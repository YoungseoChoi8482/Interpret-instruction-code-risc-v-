
#include <stdio.h>   // 표준 입출력 함수를 사용하기 위해 포함.
#include <string.h>  // 문자열 처리 함수를 사용하기 위해 포함.
#include <stdlib.h>  // 표준 라이브러리 함수를 사용하기 위해 포함.
#include <ctype.h>   // 주로 문자가 특정 유형인지 검사하거나 문자의 대소문자를 변환하는 함수들을 포함한다.
#include <stdbool.h> // boolean형을 쓰기 위해서 부른다.

#define MAX_LINE_LENGTH 1024
#define REGISTER_COUNT 32
#define INSTRUCTION_MAX_LINE 10000 // 최대 라인 길이

typedef struct
{
    char label[256];
    int address;
    int trace_address;
} Label;

Label *labels;         // 라벨 배열
int labelCount = 0;    // 라벨 개수
int labelCapacity = 0; // 라벨 배열 용량
int pc = 1000;

typedef struct
{
    char line[MAX_LINE_LENGTH]; // 한줄당 크기
    int address;                // PC 값
} Instruction;

// 레지스터 에러를 확인하는 전역변수
bool register_error = false;

Instruction *instructions = NULL; // 명령어 배열
int instructionCount = 0;         // 명령어 개수
int instructionCapacity = 0;      // 명령어 배열 용량

// 트레이스 값을 저장하는 곳
char *trace = NULL;
int traceSize = 0;

// 파일이 잘 열리는지 확인하는 함수. 만약에 syntax error가 뜰 경우 trace파일도 만들면 안되기 때문에 전역변수로 설정했다.
int fileOpenCheck = 1;

// 레지스터 배열(32개 레지스터)
int registers[REGISTER_COUNT] = {0};

// 명령어 유형 열거형
typedef enum
{
    R_TYPE,
    I_TYPE,
    S_TYPE,
    SB_TYPE,
    J_TYPE,
    EXIT_TYPE,
    UNKNOWN_TYPE
} InstructionType;

// 명령어별로 opcode와 funct3 ,fuct7를 저장한다.
typedef struct
{
    char *instName;
    InstructionType type;
    unsigned int opcode;
    unsigned int funct3;
    unsigned int funct7;
} InstructionInfo;

// 메모리 구조체 (동적 할당을 위한 구조체 정의)
typedef struct MemoryNode
{
    int address;
    int value;
    struct MemoryNode *next;
} MemoryNode;

MemoryNode *memoryHead = NULL;

InstructionInfo instructionTable[] = {
    // R-type 명령어들
    {"add", R_TYPE, 0x33, 0x0, 0x00},
    {"sub", R_TYPE, 0x33, 0x0, 0x20},
    {"and", R_TYPE, 0x33, 0x7, 0x00},
    {"or", R_TYPE, 0x33, 0x6, 0x00},
    {"xor", R_TYPE, 0x33, 0x4, 0x00},
    {"sll", R_TYPE, 0x33, 0x1, 0x00},
    {"srl", R_TYPE, 0x33, 0x5, 0x00},
    {"sra", R_TYPE, 0x33, 0x5, 0x20},

    // I-type 명령어들
    {"addi", I_TYPE, 0x13, 0x0, 0},
    {"andi", I_TYPE, 0x13, 0x7, 0},
    {"ori", I_TYPE, 0x13, 0x6, 0},
    {"xori", I_TYPE, 0x13, 0x4, 0},
    {"slli", I_TYPE, 0x13, 0x1, 0x00},
    {"srli", I_TYPE, 0x13, 0x5, 0x00},
    {"srai", I_TYPE, 0x13, 0x5, 0x20},
    {"lw", I_TYPE, 0x03, 0x2, 0},
    {"jalr", I_TYPE, 0x67, 0x0, 0},

    // S-type 명령어들
    {"sw", S_TYPE, 0x23, 0x2, 0},

    // B-type 명령어들
    {"beq", SB_TYPE, 0x63, 0x0, 0},
    {"bne", SB_TYPE, 0x63, 0x1, 0},
    {"blt", SB_TYPE, 0x63, 0x4, 0},
    {"bge", SB_TYPE, 0x63, 0x5, 0},

    // J-type 명령어들
    {"jal", J_TYPE, 0x6F, 0, 0},

    // EXIT 명령어
    {"exit", EXIT_TYPE, 0xFF, 0xF, 0xFF},

    // 배열의 끝을 표시하기 위해 NULL 추가
    {NULL, UNKNOWN_TYPE, 0, 0, 0}};

// 메모리에 값을 저장하는 함수
void storeMemory(int address, int value)
{
    MemoryNode *current = memoryHead;
    MemoryNode *prev = NULL;

    // 기존 메모리 노드가 있는지 확인
    while (current != NULL && current->address != address)
    {
        prev = current;
        current = current->next;
    }

    // 기존 메모리 노드가 있으면 값을 업데이트
    if (current != NULL)
    {
        current->value = value;
    }
    else
    {
        // 새 메모리 노드 생성
        MemoryNode *newNode = (MemoryNode *)malloc(sizeof(MemoryNode));
        newNode->address = address;
        newNode->value = value;
        newNode->next = NULL;

        // 메모리 리스트에 추가
        if (prev == NULL)
        {
            memoryHead = newNode;
        }
        else
        {
            prev->next = newNode;
        }
    }
}
// 메모리를 초기화 한다.
void freeMemory()
{
    MemoryNode *current = memoryHead;
    while (current != NULL)
    {
        MemoryNode *next = current->next; // 다음 노드를 임시로 저장
        free(current);                    // 현재 노드의 메모리 해제
        current = next;                   // 다음 노드로 이동
    }

    memoryHead = NULL; // 연결 리스트 초기화
}

// 메모리에서 값을 불러오는 함수
int loadMemory(int address)
{
    MemoryNode *current = memoryHead;

    // 메모리 노드에서 주소를 검색
    while (current != NULL)
    {
        if (current->address == address)
        {
            return current->value;
        }
        current = current->next;
    }

    // 저장되지 않은 메모리는 기본값 0을 반환
    return 0;
}

// 공백을 제거하는 함수
void trim_whitespace(char *str) {
    char *end;

    // 문자열 시작 부분의 공백 제거
    while (isspace((unsigned char)*str)) str++;

    // 문자열 끝 부분의 공백 제거
    if (*str == 0) return; // 문자열이 비어있으면 반환

    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;

    // 공백이 제거된 문자열의 끝에 널 문자 추가
    *(end + 1) = '\0';
}

// 이 함수는 주어진 명령어 줄(`line`)을 분석하여 해당 명령어의 유형(`InstructionType`)을 반환하는 역할을 합니다.
InstructionType identifyInstructionType(char *line)
{
    char instruction[20]; // 명령어를 저장할 변수

    // 새로 넣은 부분!
    // printf("instructionType trim_whitespace\n");

    // `line`의 모든 문자를 소문자로 변환
    for (int i = 0; line[i]; i++)
    {
        line[i] = tolower(line[i]);
    }

    // `line`에서 첫 번째 단어(명령어 부분)를 추출
    sscanf(line, "%s", instruction);

    // instructionTable을 순회하며 정확히 일치하는 명령어를 찾음
    for (int i = 0; instructionTable[i].instName != NULL; i++)
    {
        if (strcmp(instruction, instructionTable[i].instName) == 0)
        { // 정확히 일치하는지 확인 예를들어 addi같은 경우는 add가 있기 때문에 이 과정이 없으면 R-type으로 오해될 수도 있다.
            return instructionTable[i].type;
        }
    }

    return UNKNOWN_TYPE; // 일치하는 명령어가 없을 경우
}

// 레지스터 문자열에서 숫자만 추출하는 함수
unsigned int extractRegisterNumber(char *reg)
{
    // 앞에 공백이 있는 경우 다음 문자로 넘어감
    while (*reg == ' ' || *reg == '\t') {
        reg++;
    }

    // 예: "x5"에서 숫자 5를 추출  , xx12같은것을 막기 위해
    if ((reg[0] == 'x' || reg[0] == 'X') && isdigit(reg[1]))
    {
        int reg_num = atoi(&reg[1]);

        // 레지스터 번호가 0~31 사이인지 확인
        if (reg_num >= 0 && reg_num <= 31)
        {
            return (unsigned int)reg_num;
        }
        else
        {
            register_error = true;
            return 0; // 에러 코드나 다른 처리 필요
        }
    }
    else
    {
        register_error = true;
        return 0; // 에러 코드나 다른 처리 필요
    }
}

// 오버플로우인지 체크하는 함수
bool is_overflow(int value, InstructionType type)
{

    if (type == I_TYPE || type == S_TYPE || type == SB_TYPE)
    {
        // 12비트 부호 있는 즉시값 범위 확인
        if (value < -2048 || value > 2047)
        {
            return true; // 오버플로우 발생
        }
        else
        {
            return false; // 오버플로우 없음
        }
    }
    else
    {
        if (value < -524288 || value > 524287)
        {
            return true; // 오버플로우 발생
        }
        else
        {
            return false; // 오버플로우 없음
        }
    }
}

// 이진수로 바꾸는 함수
void writeBinary(FILE *file, unsigned int value)
{
    for (int i = 31; i >= 0; i--)
    {
        fprintf(file, "%d", (value >> i) & 1);
    }
    fprintf(file, "\n"); // 줄바꿈
}

// 레이블의 주소를 찾는 함수
int findLabelAddress(char *label)
{
    for (int i = 0; i < labelCount; i++)
    {
        if (strcmp(labels[i].label, label) == 0)
        {
            return labels[i].address;
        }
    }
    return -1; // 찾지 못한 경우
}

// 명령어를 실질적으로 계산하는 함수(레지스터 계산 포함)
int executeInstruction(char *line, InstructionType type)
{
    unsigned int rd, rs1, rs2, imm;
    char reg1[10], reg2[10], reg3[10];
    char instruction[200];

    // line에서 명령어 추출
    sscanf(line, "%s", instruction);

    // R-type 명령어 처리
    if (type == R_TYPE)
    {
        sscanf(line, "%*s %[^,],%[^,],%s", reg1, reg2, reg3);
        //sscanf(line, "%*s %s %s %s", reg1, reg2, reg3);
        rd = extractRegisterNumber(reg1);
        rs1 = extractRegisterNumber(reg2);
        rs2 = extractRegisterNumber(reg3);

        if (strcmp(instruction, "add") == 0)
        {
            registers[rd] = registers[rs1] + registers[rs2];
        }
        else if (strcmp(instruction, "sub") == 0)
        {
            registers[rd] = registers[rs1] - registers[rs2];
        }
        else if (strcmp(instruction, "and") == 0)
        {
            registers[rd] = registers[rs1] & registers[rs2];
        }
        else if (strcmp(instruction, "or") == 0)
        {
            registers[rd] = registers[rs1] | registers[rs2];
        }
        else if (strcmp(instruction, "xor") == 0)
        {
            registers[rd] = registers[rs1] ^ registers[rs2];
        }
        else if (strcmp(instruction, "sll") == 0)
        {
            registers[rd] = registers[rs1] << (registers[rs2] & 0x1F);
        }
        else if (strcmp(instruction, "srl") == 0)
        {
            registers[rd] = ((unsigned int)registers[rs1]) >> (registers[rs2] & 0x1F);
        }
        else if (strcmp(instruction, "sra") == 0)
        {
            // 산술 시프트 연산: 부호 비트를 유지하며 시프트
            int shiftAmount = registers[rs2] & 0x1F;
            registers[rd] = registers[rs1] >> shiftAmount;
            if (registers[rs1] < 0)
            {
                // 음수인 경우 상위 비트를 1로 채움 (부호 비트 유지)
                unsigned int mask = (1 << (32 - shiftAmount)) - 1;
                registers[rd] |= ~mask;
            }
        }
        pc = pc + 4;
    }
    // I-type 명령어 처리
    else if (type == I_TYPE)
    {

        if (strcmp(instruction, "lw") == 0)
        {
            sscanf(line, "%*s %[^,],%d(%[^)])", reg1, &imm, reg2);
            //sscanf(line, "%*s %[^,], %d(%[^)])", reg1, &imm, reg2);
            rd = extractRegisterNumber(reg1);
            rs1 = extractRegisterNumber(reg2);
            int address = registers[rs1] + imm;
            registers[rd] = loadMemory(address);

            pc = pc + 4;
        }
        else if (strcmp(instruction, "jalr") == 0)
        {
            sscanf(line, "%*s %[^,],%d(%[^)])", reg1, &imm, reg2);
            //sscanf(line, "%*s %[^,], %d(%[^)])", reg1, &imm, reg2);
            rd = extractRegisterNumber(reg1);
            rs1 = extractRegisterNumber(reg2);
            int next_pc = pc + 4; // 다음 명령어 주소 저장

            pc = (registers[rs1] + imm) & ~1; // 목표 주소로 점프

            if (rd != 0)
            {
                registers[rd] = next_pc; // 반환 주소 저장 (rd가 x0이 아닌 경우)
            }
        }
        else
        {
            sscanf(line, "%*s %[^,],%[^,],%d", reg1, reg2, &imm);
            //sscanf(line, "%*s %s %s %d", reg1, reg2, &imm);
            rd = extractRegisterNumber(reg1);
            rs1 = extractRegisterNumber(reg2);

            if (strcmp(instruction, "addi") == 0)
            {
                registers[rd] = registers[rs1] + imm;
            }
            else if (strcmp(instruction, "andi") == 0)
            {
                registers[rd] = registers[rs1] & imm;
            }
            else if (strcmp(instruction, "ori") == 0)
            {
                registers[rd] = registers[rs1] | imm;
            }
            else if (strcmp(instruction, "xori") == 0)
            {
                registers[rd] = registers[rs1] ^ imm;
            }
            else if (strcmp(instruction, "slli") == 0)
            {
                registers[rd] = registers[rs1] << (imm & 0x1F);
            }
            else if (strcmp(instruction, "srli") == 0)
            {
                registers[rd] = ((unsigned int)registers[rs1]) >> (imm & 0x1F);
            }
            else if (strcmp(instruction, "srai") == 0)
            {
                // 산술 시프트 연산: 부호 비트를 유지하며 시프트
                int shiftAmount = imm & 0x1F;
                registers[rd] = registers[rs1] >> shiftAmount;
                if (registers[rs1] < 0)
                {
                    // 음수인 경우 상위 비트를 1로 채움 (부호 비트 유지)
                    unsigned int mask = (1 << shiftAmount) - 1;
                    registers[rd] |= mask << (32 - shiftAmount);
                }
            }
            pc = pc + 4;
        }
    }
    // S-type 명령어 처리
    else if (type == S_TYPE)
    {
        sscanf(line, "%*s %[^,],%d(%[^)])", reg1, &imm, reg2);
        //sscanf(line, "%*s %[^,], %d(%[^)])", reg1, &imm, reg2);
        rs1 = extractRegisterNumber(reg2);
        rs2 = extractRegisterNumber(reg1);

        if (strcmp(instruction, "sw") == 0)
        {
            int address = registers[rs1] + imm;
            storeMemory(address, registers[rs2]);
        }

        pc = pc + 4;
    }
    // SB-type 명령어 처리
    else if (type == SB_TYPE)
    {
        sscanf(line, "%*s %[^,],%[^,],%s", reg1, reg2, reg3);
        //sscanf(line, "%*s %s %s %s", reg1, reg2, reg3);
        rs1 = extractRegisterNumber(reg1);
        rs2 = extractRegisterNumber(reg2);
        int labelAddress = findLabelAddress(reg3);

        // 없는 레이블로 갈 경우 에러처리 해야함.
        if (labelAddress == -1)
        {
            // printf("Label not found: %s\n", reg3);
        }

        // 일단 여기서 imm 값은 label이랑 얼마나 떨어져있냐가 관건이다.
        imm = labelAddress - pc;

        if (strcmp(instruction, "beq") == 0)
        {
            if (registers[rs1] == registers[rs2])
            {
                pc += imm;
            }
            else
            {
                pc = pc + 4;
            }
        }
        else if (strcmp(instruction, "bne") == 0)
        {
            if (registers[rs1] != registers[rs2])
            {
                pc += imm;
            }
            else
            {
                pc = pc + 4;
            }
        }
        else if (strcmp(instruction, "blt") == 0)
        {
            if (registers[rs1] < registers[rs2])
            {
                pc += imm;
            }
            else
            {
                pc = pc + 4;
            }
        }
        else if (strcmp(instruction, "bge") == 0)
        {
            if (registers[rs1] >= registers[rs2])
            {
                pc += imm;
            }
            else
            {
                pc = pc + 4;
            }
        }
    }
    // J-type 명령어 처리
    else if (type == J_TYPE)
    {
        if (strcmp(instruction, "jal") == 0)
        {
            sscanf(line, "%*s %[^,],%s", reg1, reg3);
            //sscanf(line, "%*s %s %s", reg1, reg3);
            rd = extractRegisterNumber(reg1);
            int labelAddress = findLabelAddress(reg3);

            // 없는 레이블로 갈 경우 에러처리 해야함.
            if (labelAddress == -1)
            {
                // printf("Label not found: %s\n", reg3);
            }

            // 일단 여기서 imm 값은 label이랑 얼마나 떨어져있냐가 관건이다.
            imm = labelAddress - pc;

            registers[rd] = pc + 4; // x1 레지스터에 return 주소 저장
            pc += imm;
        }
    }
    registers[0] = 0;
    return pc;
}

// trace포인터에 pc값을 저장하는 저장하는 함수 (해석 필요)
void appendToTrace(int number)
{
    char buffer[20]; // 숫자와 공백을 임시로 저장할 버퍼

    snprintf(buffer, sizeof(buffer), " %d ", number); // 숫자와 공백을 버퍼에 저장

    // trace 크기를 동적으로 늘리기 위해 realloc 사용
    int neededSize = (trace ? strlen(trace) : 0) + strlen(buffer) + 1;
    if (neededSize > traceSize)
    {
        traceSize = neededSize * 100; // 크기를 여유 있게 늘립니다.
        char *newTrace = (char *)realloc(trace, traceSize);
        if (newTrace == NULL)
        {
            // realloc 실패 시 메모리 누수 방지를 위해 함수 종료
            printf("Memory reallocation failed.\n");
            return;
        }
        trace = newTrace;
    }
    // 방금 추가한 부분!!
    if (trace != NULL)
    {
        // 버퍼 내용을 trace에 추가(처음과 이후의 구분 없이 strcat 사용)
        strcat(trace, buffer);
    }
}

// 특정 PC 값에 해당하는 명령어를 찾는 함수
Instruction *fetchInstruction(int pcValue)
{
    for (int i = 0; i < instructionCount; i++)
    {
        if (instructions[i].address == pcValue)
        {
            return &instructions[i]; // 해당 PC 값을 가진 명령어를 반환
        }
    }
    return NULL; // 해당 PC 값에 대한 명령어를 찾지 못한 경우
}

// 명령어를 넣으면 파일을 실행한다. 명령어 한줄씩 읽으면서 값을 정한다.
void executeProgram()
{
    pc = 1000; // 프로그램 시작 시 PC 초기값

    while (1)
    {
        // 현재 PC에 해당하는 명령어 가져오기
        Instruction *instr = fetchInstruction(pc);
        if (instr == NULL)
        {
            break; // 더 이상 명령어가 없으면 종료
        }

        // 현재 PC값을 트레이스에 저장
        appendToTrace(pc);

        // 명령어 유형 식별 (예: R_TYPE, I_TYPE, 등)
        InstructionType type = identifyInstructionType(instr->line);

        // 유효하지 않은 명령어인 경우, 오류 처리
        if (type == UNKNOWN_TYPE)
        {
            break;
        }

        // 현재 pc 값을 저장한다 무한루프 에러체크를 위해서
        int nowPc = pc;

        // 명령어 실행 (명령어에 따라 레지스터 변경, 메모리 접근, PC 갱신 등)
        int newPc = executeInstruction(instr->line, type);

        // 종료 명령어인 경우 프로그램 종료
        if (strcmp(instr->line, "exit") == 0)
        {
            break;
        }

        if (newPc == nowPc)
        {
            break;
        }

        // PC 갱신: 명령어 실행 후 PC 값이 업데이트되었는지 확인
        // 일반적인 경우에는 executeInstruction 함수 내부에서 pc += 4로 설정하지만
        // 분기나 점프의 경우 PC가 바뀔 수 있음
        pc = newPc;
    }
}

// 라벨(레이블)의 위치를 정확히 알아야 분기문하고 trace값을 처리할 수 있다. 그래서 처음에 레이블이 있는지 한번 훑는다.
// 첫 번째 패스: 레이블 주소 저장
bool firstPass(const char *filename)
{
    FILE *inputFile = fopen(filename, "r");
    char line[MAX_LINE_LENGTH];

    // 레이블 에러가 있을 경우.
    bool label_Error = false;

    // Program counter 초기값 (가정) . 여기서는 임의로 한번 읽는것이기 때문에 pc값을 따로 뺀다.
    pc = 1000;

    // 파일 끝까지 읽음
    while (fgets(line, sizeof(line), inputFile) != NULL)
    {
        // 줄 끝의 개행 문자 제거
        line[strcspn(line, "\n")] = '\0';

        // 비어있는 줄은 넘어간다.
        if (strlen(line) == 0)
        {
            continue;
        }
        // `line`의 모든 문자를 소문자로 변환
        for (int i = 0; line[i]; i++)
        {
            line[i] = tolower(line[i]);
        }
        // 라벨 확인. ":"가 오는부분으로 labelPos에 저장하고 그곳에 널문자를 넣는다. 그러면 자연스럽게 label이름만 추출이 가능하다.
        char *labelPos = strchr(line, ':');
        if (labelPos != NULL)
        {
            *labelPos = '\0'; // ':' 문자를 NULL로 대체하여 레이블 이름만 추출
            Label newLabel;
            strcpy(newLabel.label, line);
            newLabel.address = pc;
            // 기존에 같은 이름의 라벨이 있는지 확인
            for (int i = 0; i < labelCount; i++)
            {
                if (strcmp(labels[i].label, newLabel.label) == 0)
                {
                    label_Error = true; // 중복 라벨 발견
                    break;
                }
            }

            // 라벨 배열에 추가 (중복 체크 후)
            if (labelCount >= labelCapacity)
            {
                labelCapacity += 10000;
                labels = realloc(labels, labelCapacity * sizeof(Label));
            }
            labels[labelCount++] = newLabel; // 중복 여부와 관계없이 라벨 추가
        }
        else
        {
            // 명령어 한 줄이므로 PC를 증가시킴
            pc += 4;
        }
    }

    if (label_Error == true)
    {
        return true;
    }

    rewind(inputFile); // 파일 포인터를 다시 처음으로 이동
    fclose(inputFile); // 파일을 닫는다.
    return false;
}

// 두 번쨰 패스. 실제로 명령어를 계산하고 명령어를 해석(이진수 변환)하는 부분.
bool processFile(const char *filename)
{
    FILE *inputFile = fopen(filename, "r");
    char outputFilename[260];
    pc = 1000;
    bool isThereError = false;
    // 파일명.o  파일을 만드는 것
    snprintf(outputFilename, sizeof(outputFilename), "%s.o", strtok(strdup(filename), "."));
    FILE *outputFile = fopen(outputFilename, "w");
    fileOpenCheck = 1;

    char line[MAX_LINE_LENGTH];

    if (inputFile == NULL || outputFile == NULL)
    {
        printf("we can't open the file\n");
        return false;
    }

    //  파일 끝까지 읽는다.
    while (fgets(line, sizeof(line), inputFile) != NULL)
    {
        // 줄 끝의 개행 문자 제거
        line[strcspn(line, "\n")] = '\0';

        // 비어있는 줄은 넘어간다.
        if (strlen(line) == 0)
        {
            continue;
        }

        // 라벨 값은 그냥 넘어간다.
        if (strchr(line, ':') != NULL)
        {

            continue;
        }

        // 명령어의 유형을 판별 , 그리고 여기서 명령어를 모두 소문자로 만들어준다!!
        InstructionType type = identifyInstructionType(line);

        // 문법에 맞지 않는 assembly 코드가 하나라도 존재하는 경우에
        // Syntax Error을 출력하고 새로운 파일 입력을 대기함. 파일명.o ,파일명.trace파일을 생성하지 않음
        if (type == UNKNOWN_TYPE)
        {
            printf("Syntax Error!!\n");
            fileOpenCheck = 0;
            break;
        }

        // 여기서부터 파일을 한줄씩 입력받으며 이진수로 새로운 파일에 적는다.
        for (int i = 0; instructionTable[i].instName != NULL; i++)
        {
            // 첫단어를 입력받는다.
            char instruction[20];
            sscanf(line, "%s", instruction);

            // 입력받은 첫 단어를 구조체 안에서 탐색한다.
            if (strcmp(instruction, instructionTable[i].instName) == 0)
            {
                unsigned int opcode = instructionTable[i].opcode;
                unsigned int funct3 = instructionTable[i].funct3;
                unsigned int funct7 = instructionTable[i].funct7;
                unsigned int rd, rs1, rs2, imm;
                char reg1[10], reg2[10], reg3[10];

                // R-type 명령어 처리
                if (type == R_TYPE)
                {
                    // R-type 형식: opcode (7 bits) | rd (5 bits) | funct3 (3 bits) | rs1 (5 bits) | rs2 (5 bits) | funct7 (7 bits)

                    // 12_1 최종수정
                    sscanf(line, "%*s %[^,],%[^,],%s", reg1, reg2, reg3);

                   
                    rd = extractRegisterNumber(reg1);
                    rs1 = extractRegisterNumber(reg2);
                    rs2 = extractRegisterNumber(reg3);
                    unsigned int instruction = (funct7 << 25) | (rs2 << 20) | (rs1 << 15) | (funct3 << 12) | (rd << 7) | opcode;
                    writeBinary(outputFile, instruction);
                }
                // I-type 명령어 처리
                else if (type == I_TYPE)
                {
                    // I-type 형식: imm(12bits) | rs1 (5 bits) | funct3 (3 bits) | rd (5 bits) | opcode (7 bits)

                    //'lw'와 'jalr' 은 명령어 명령어 형식이 조금다르니 다르게 받는다.
                    if (instructionTable[i].instName == "lw" || instructionTable[i].instName == "jalr")
                    {
                        sscanf(line, "%*s %[^,],%d(%[^)])", reg1, &imm, reg2);
                        rd = extractRegisterNumber(reg1);
                        rs1 = extractRegisterNumber(reg2);
                        isThereError = is_overflow(imm, type);
                        imm = (imm & 0xFFFFFFFF);

                        unsigned int instruction = (imm << 20) | (rs1 << 15) | (funct3 << 12) | (rd << 7) | opcode;
                        writeBinary(outputFile, instruction);
                    }
                    else if (instructionTable[i].instName == "srli" || instructionTable[i].instName == "slli" || instructionTable[i].instName == "srai")
                    {
                        sscanf(line, "%*s %[^,],%[^,],%d", reg1, reg2, &imm);
                        //sscanf(line, "%*s %s %s %d", reg1, reg2, &imm);

                        rd = extractRegisterNumber(reg1);
                        rs1 = extractRegisterNumber(reg2);
                        if (imm > 32)
                        {
                            isThereError = true;
                        }
                        imm = (imm & 0x1F); // sign-extension 처리
                        unsigned int instruction = (funct7 << 25) | (imm << 20) | (rs1 << 15) | (funct3 << 12) | (rd << 7) | opcode;
                        writeBinary(outputFile, instruction);
                    }
                    else
                    {
                        sscanf(line, "%*s %[^,],%[^,],%d", reg1, reg2, &imm);
                        rd = extractRegisterNumber(reg1);
                        rs1 = extractRegisterNumber(reg2);
                        isThereError = is_overflow(imm, type);
                        imm = (imm & 0xFFFFFFFF); // sign-extension 처리
                        unsigned int instruction = (imm << 20) | (rs1 << 15) | (funct3 << 12) | (rd << 7) | opcode;
                        writeBinary(outputFile, instruction);
                    }
                }
                // S-type 명령어 처리
                else if (type == S_TYPE)
                {
                    // S=type 형식: imm[11:5] (7 bits) | rs2 (5 bits) | rs1 (5 bits) | funct3 (3 bits) | imm[4-1:11] (5 bits) | opcode (7 bits)
                    sscanf(line, "%*s %[^,],%d(%[^)])", reg1, &imm, reg2);
                    rs2 = extractRegisterNumber(reg1);
                    rs1 = extractRegisterNumber(reg2);
                    isThereError = is_overflow(imm, type);
                    unsigned int imm11_5 = (imm >> 5) & 0x7F;
                    unsigned int imm4_0 = imm & 0x1F;
                    unsigned int instruction = (imm11_5 << 25) | (rs2 << 20) | (rs1 << 15) | (funct3 << 12) | (imm4_0 << 7) | opcode;
                    writeBinary(outputFile, instruction);
                }
                // SB-type 명령어 처리
                else if (type == SB_TYPE)
                {

                    // SB-type 형식: imm[12|10:5] (7 bits) | rs2 (5 bits) | rs1 (5 bits) | funct3 (3 bits) | imm[4:1|11] (5 bits) | opcode (7 bits)
                    char immStr[50];
                    sscanf(line, "%*s %[^,],%[^,],%s", reg1, reg2, immStr);
                    int labelAddress = 0;

                    rs1 = extractRegisterNumber(reg1);
                    rs2 = extractRegisterNumber(reg2);
                    int isLabel = 0;
                    int offset = 0;

                    // immStr이 정수 값인지 레이블인지를 판단합니다.
                    if (isdigit(immStr[0]) || immStr[0] == '-')
                    {
                        // 정수형 즉시 값인 경우
                        offset = atoi(immStr);
                        isThereError = is_overflow(offset, type);
                    }
                    else
                    {
                        // 레이블인 경우, 레이블의 주소를 찾아야 합니다.
                        labelAddress = findLabelAddress(immStr);
                        if (labelAddress == -1)
                        {
                            // Syntax error로 고쳐야한다!!! ()
                            // printf("Label not found: %s\n", immStr);
                            isThereError = true;
                            break;
                        }
                        // 현재 PC와 레이블 주소의 차이를 계산해 offset으로 사용합니다.
                        offset = (labelAddress - pc) / 2; // SB 타입에서는 offset을 명령어 주소의 차이로 계산해야 합니다.
                        isLabel = 1;
                    }

                    // Branch 명령어의 즉시 값을 비트로 분해합니다.
                    offset = (offset << 1);
                    unsigned int imm12 = (offset >> 12) & 0x1;
                    unsigned int imm10_5 = (offset >> 5) & 0x3F;
                    unsigned int imm4_1 = (offset >> 1) & 0xF;
                    unsigned int imm11 = (offset >> 11) & 0x1;
                    unsigned int instruction = (imm12 << 31) | (imm10_5 << 25) | (rs2 << 20) | (rs1 << 15) | (funct3 << 12) | (imm4_1 << 8) | (imm11 << 7) | opcode;

                    // 이진수로 출력
                    writeBinary(outputFile, instruction);
                }
                // J-type 명령어 처리
                else if (type == J_TYPE)
                {
                    // J - type 형식: imm[20:10-1:11:19-12] (20 bits) | rd (5 bits) | opcode (7 bits)
                    char immStr[50];
                    
                    sscanf(line, "%*s %[^,],%s", reg1, immStr);
                    //sscanf(line, "%*s %s %s", reg1, immStr);

            
                    int labelAddress = 0;

                    rd = extractRegisterNumber(reg1);

                

                    int isLabel = 0;
                    int offset = 0;

                    // immStr이 정수 값인지 레이블인지를 판단합니다.
                    if (isdigit(immStr[0]) || immStr[0] == '-')
                    {
                        // 정수형 즉시 값인 경우
                        offset = atoi(immStr);
                        isThereError = is_overflow(offset, type);
                    }
                    else
                    {
                        // 레이블인 경우, 레이블의 주소를 찾아야 합니다.
                        labelAddress = findLabelAddress(immStr);
                        if (labelAddress == -1)
                        {
                            // Syntax error로 고쳐야한다!!! ()
                            // printf("Label not found: %s\n", immStr);
                            isThereError = true;
                            break;
                        }
                        // 현재 PC와 레이블 주소의 차이를 계산해 offset으로 사용합니다.
                        offset = (labelAddress - pc) / 2; // SB 타입에서는 offset을 명령어 주소의 차이로 계산해야 합니다.
                        isLabel = 1;
                    }

                    offset = (offset << 1);
                    imm = offset;
                    unsigned int imm20 = imm & 0x1;
                    unsigned int imm10_1 = (imm >> 1) & 0x3FF;
                    unsigned int imm11 = (imm >> 11) & 0x1;
                    unsigned int imm19_12 = (imm >> 12) & 0xFF;
                    unsigned int instruction = (imm20 << 31) | (imm10_1 << 21) | (imm11 << 20) | (imm19_12 << 12) | (rd << 7) | opcode;
                    writeBinary(outputFile, instruction);
                }

                // EXIT type 명령어 처리
                else if (type == EXIT_TYPE)
                {
                    unsigned int instruction = 0xFFFFFFFF;
                    writeBinary(outputFile, instruction);
                }
                break;
            }
        }
        if (isThereError == true || register_error == true)
        {
            break;
        }
        pc = pc + 4;
    }

    if (fileOpenCheck == 1 && register_error == false && isThereError == false)
    {

        fclose(inputFile);
        fclose(outputFile);
        return true;
    }
    else
    {
        fclose(inputFile);
        fclose(outputFile);
        remove(outputFilename);
        return false;
    }
}

// 세 번째 패스. 파일을 전체적으로 읽으면서 모든 명령어와 주소를 배열에 넣는다
void loadInstructions(const char *filename)
{
    FILE *inputFile = fopen(filename, "r");

    // printf("loadInstructions file open success!"); 여기까지는 잘됨

    char line[MAX_LINE_LENGTH];
    pc = 1000; // PC 초기값

    if (inputFile == NULL)
    {
        printf("we can't open the file\n");
        return;
    }

    while (fgets(line, sizeof(line), inputFile) != NULL)
    {
        // 줄 끝의 개행 문자 제거
        line[strcspn(line, "\n")] = '\0';

        // 비어있는 줄은 넘어간다.
        if (strlen(line) == 0)
        {
            continue;
        }

        // // 줄 끝의 개행 문자 제거
        // line[strcspn(line, "\n")] = '\0';

        // 라벨 확인. 현재 :문자가 있는 위치를 찾는다.
        char *labelPos = strchr(line, ':');
        if (labelPos != NULL)
        {
            *labelPos = '\0'; // ':'를 '\0'로 대체하여 레이블 이름만 추출
            Label newLabel;
            strcpy(newLabel.label, line);
            newLabel.address = pc;

            // 라벨 배열에 추가
            if (labelCount >= labelCapacity)
            {
                labelCapacity += 10;
                labels = realloc(labels, labelCapacity * sizeof(Label));
            }
            labels[labelCount++] = newLabel;

            // 명령어 부분 추출
            char *instructionPart = labelPos + 1;
            while (isspace(*instructionPart))
                instructionPart++;

            if (*instructionPart == '\0')
            {
                // 명령어가 없으면 다음 줄로 이동
                continue;
            }
            else
            {
                // 명령어 배열에 추가
                if (instructionCount >= instructionCapacity)
                {
                    instructionCapacity += 100;
                    instructions = realloc(instructions, instructionCapacity * sizeof(Instruction));
                }

                strcpy(instructions[instructionCount].line, instructionPart);
                instructions[instructionCount].address = pc;
                instructionCount++;

                pc += 4;
            }
        }
        else
        {
            // 공백 또는 주석 라인 처리
            char *trimmedLine = line;
            while (isspace(*trimmedLine))
                trimmedLine++;
            if (*trimmedLine == '\0' || *trimmedLine == '#')
            {
                continue;
            }

            // 명령어 배열에 추가
            if (instructionCount >= instructionCapacity)
            {
                instructionCapacity += 10000;
                instructions = realloc(instructions, instructionCapacity * sizeof(Instruction));
            }

            strcpy(instructions[instructionCount].line, line);
            instructions[instructionCount].address = pc;
            instructionCount++;

            pc += 4;
        }
    }

    fclose(inputFile);
}

// 트레이스 파일을 만들어 보자. trace에 있는 값을 한줄씩 저장한다.
void traceFile(const char *filename)
{
    FILE *inputFile = fopen(filename, "r");
    char outputFilename[260];

    // 파일명.trace  파일을 만드는 것
    snprintf(outputFilename, sizeof(outputFilename), "%s.trace", strtok(strdup(filename), "."));
    FILE *outputFile = fopen(outputFilename, "w");
    char line[MAX_LINE_LENGTH];

    // 파일이 열리지 않을 때는 이렇게 처리한다.
    if (inputFile == NULL || outputFile == NULL)
    {
        printf("we can't open the file\n");
        return;
    }

    // 일단 파일을 한번 싹 읽고 모든명령어와 라벨의 주소값을 저장한다.!!!
    loadInstructions(filename);

    // trace에 값을 저장!!
    executeProgram();

    char *token = strtok(trace, " "); // 공백을 기준으로 첫 번째 토큰 추출

    while (token != NULL)
    {
        int value;
        if (sscanf(token, "%d", &value) == 1) // 숫자 변환 확인
        {
            fprintf(outputFile, "%d\n", value); // 파일에 숫자 기록
        }
        else
        {
            // 숫자가 아닌 경우 로그를 출력하거나 무시할 수 있습니다.
        }

        // 다음 공백을 기준으로 토큰 추출
        token = strtok(NULL, " ");
    }
    // 파일에 빈줄이 하나생기는데 그정도는 상관없는지 교수님께 물어보자.

    // Syntax error 가 떴을 때 trace파일 자체를 쓰지 않기 위해서 넣었다.
    if (fileOpenCheck)
    {
        fclose(inputFile);
        fclose(outputFile);
    }
    else
    {
        remove(outputFilename);
        return;
    }
}

//
// 에러 체크 구간!! 시작!!
//
// 괄호에러를 확인하는 함수이다.
bool check_bracket_balance(char *line)
{
    // 괄호 개수 체크를 위한 변수 초기화
    int bracket_count = 0;
    for (int i = 0; i < strlen(line); i++)
    {
        if (line[i] == '(')
        {
            bracket_count++;
        }
        else if (line[i] == ')')
        {
            bracket_count--;
            if (bracket_count < 0)
            {
                return false; // 닫는 괄호가 여는 괄호보다 먼저 나오는 경우
            }
        }
    }

    // 추가: 라인에 괄호가 없거나 잘못된 경우에 대한 체크
    if (bracket_count != 0)
    {
        return false; // 여는 괄호가 남아있는 경우
    }

    return true; // 괄호가 균형을 이루는 경우
}

// 명령어 에러를 확인하는 함수이다.
bool check_instruction_validity(char *check_instruction_validity_line)
{

    char instruction_name[MAX_LINE_LENGTH];
    sscanf(check_instruction_validity_line, "%s", instruction_name);

    // 대소문자는 상관없으니 모든 문자를 소문자로 내린다.
    for (int i = 0; instruction_name[i]; i++)
    {
        instruction_name[i] = tolower(check_instruction_validity_line[i]);
    }

    for (int i = 0; instructionTable[i].instName != NULL; i++)
    {
        if (strcmp(instruction_name, instructionTable[i].instName) == 0)
        {
            return true; // 유효한 명령어인 경우
        }
    }

    return false; // 유효하지 않은 명령어인 경우
}

// 에러가 있는지 확인하기 위한 변수.
bool has_error;

// 에러 체킹 함수
bool check_file_for_errors(const char *filename)
{
    FILE *file = fopen(filename, "r");

    char check_error_line[MAX_LINE_LENGTH];
    int line_number = 0;
    has_error = false;

    while (fgets(check_error_line, sizeof(check_error_line), file) != NULL)
    {
        line_number++;

        // 줄 끝의 개행 문자 제거
    check_error_line[strcspn(check_error_line, "\n")] = '\0';

        //비어있으면 건너 뜀.
        if (strlen(check_error_line) == 0)
        {
            continue;
        }

        // 공백 제거 로직을 직접 여기에 포함
        // 문자열 시작 부분의 공백 제거
        char *start = check_error_line;
        while (isspace((unsigned char)*start))
        {
            start++;
        }

        // 문자열 끝 부분의 공백 제거
        char *end = start + strlen(start) - 1;
        while (end > start && isspace((unsigned char)*end))
        {
            end--;
        }

        // 공백이 제거된 문자열의 끝에 널 문자 추가
        *(end + 1) = '\0';

        // 공백이 제거된 문자열을 check_error_line에 복사
        strcpy(check_error_line, start);

        // 공백이 제거된 문자열을 소문자로 변환
        for (int i = 0; check_error_line[i]; i++)
        {
            check_error_line[i] = tolower((unsigned char)check_error_line[i]);
        }

        char line_copy[MAX_LINE_LENGTH];
        strcpy(line_copy, check_error_line);

        // 라벨 값은 그냥 넘어간다. 하지만 ':' 뒤에 잘못된 내용이 있는지 확인한다.
        char *colon_pos = strchr(check_error_line, ':');
        // 라벨 값은 그냥 넘어간다.
        if (colon_pos != NULL)
        {
            // ':' 이후에 공백 문자들만 있으면 올바른 라벨로 간주
            colon_pos++; // ':' 다음 위치로 이동
            while (*colon_pos != '\0')
            {
                if (!isspace((unsigned char)*colon_pos)) // 공백이 아닌 문자가 있다면 에러 처리
                {
                    has_error = true;
                    break;
                }
                colon_pos++;
            }

            // 라벨이 있는 라인은 명령어 처리 없이 건너뜀
            if (!has_error)
            {
                continue;
            }
        }

        if (identifyInstructionType(check_error_line) != EXIT_TYPE)
        {
            // 쉼표가 두 개 연속으로 나오는지 확인
            int comma_error = 0;
            for (int i = 0; check_error_line[i] != '\0'; i++)
            {
                if (check_error_line[i] == ',' && (check_error_line[i + 1] == ',' || check_error_line[i + 1] == '\0'))
                {
                    comma_error = 1;
                    break;
                }
            }

            if (comma_error)
            {
                has_error = true;
                break;
            }
            else
            {

                // 토큰 개수가 3개인지 확인한다.
                char *token = strtok(check_error_line, ",()");
                int token_count = 0;
                char *tokens[3]; // 최대 3개의 토큰 저장
                while (token != NULL)
                {
                    if (token != NULL) // 빈 문자열이 아닌 경우만 처리
                    {
                        if (token_count < 4)
                        {
                            tokens[token_count] = token; // 토큰 저장
                        }
                        token_count++;
                    }
                    token = strtok(NULL, ",()");
                }

                if (identifyInstructionType(check_error_line) == J_TYPE && token_count != 2)
                {

                    has_error = true;
                    break;
                }
                else if (identifyInstructionType(check_error_line) != J_TYPE && token_count != 3 && identifyInstructionType(check_error_line) != EXIT_TYPE)
                {
                    // printf("error identify instruction type\n");
                    has_error = true;
                    break;
                }

                // 맨 앞 문자열만 가져오기
                char first_token[MAX_LINE_LENGTH];
                sscanf(check_error_line, "%s", first_token);

                // I-타입 명령어의 세 번째 토큰이 정수인지 확인
                if (identifyInstructionType(check_error_line) == I_TYPE && strcmp(first_token, "jalr") != 0 && strcmp(first_token, "lw") != 0)
                {
                    char *last_operand = tokens[2]; // 세 번째 토큰
                    char *endptr;
                    long immediate = strtol(last_operand, &endptr, 10);

                    if (*endptr != '\0')
                    {
                        // 변환되지 않은 문자가 남아있음 -> 정수가 아님
                        has_error = true;
                        break;
                    }
                }
            }
        }

        bool bracket_check = check_bracket_balance(line_copy);

        // 괄호 균형 확인
        if (bracket_check == false)
        {
            has_error = true;
            break;
        }

        bool instruction_check = check_instruction_validity(line_copy);
        // 명령어 유효성 확인
        if (instruction_check == false)
        {
            has_error = true;
            break;
        }
    }

    fclose(file);

    // 에러가 있는지 없는지를 리턴한다.
    return has_error;
}

int main()
{

    char filename[256]; // 입력 파일 이름을 저장할 배열
    FILE *inputFile;

    while (1)
    {
        // 파일을 여러번 읽을 수도 있기 때문에 초기화를 시켜준다. (아직 부족한게 있을 수도)
        labels = NULL;
        labelCount = 0;
        labelCapacity = 0;
        instructions = NULL;
        trace = NULL;
        // trace값을 다시 초기화한다.
        trace = (char *)calloc(traceSize, sizeof(char));
        traceSize = 0;
        pc = 1000;
        instructionCapacity = 0;
        instructionCount = 0;
        register_error = false;

        // register 값을 초기화 한다.
        for (int i = 0; i < 32; i++)
        {
            registers[i] = 0;
        }

        // register 값을 선언하고 들어간다.
        registers[1] = 1;
        registers[2] = 2;
        registers[3] = 3;
        registers[4] = 4;
        registers[5] = 5;
        registers[6] = 6;

        printf(">>Enter Input File Name: ");
        scanf("%s", filename);

        // "terminate"가 입력되면 프로그램 종료
        if (strcmp(filename, "terminate") == 0)
        {
            break;
        }

        // 파일 존재 여부 확인
        inputFile = fopen(filename, "r");
        if (inputFile == NULL)
        {
            printf("Input file does not exist!!\n");
            continue;
        }

        // 파일 닫기
        fclose(inputFile);

        // 일단 파일을 읽으면서 에러가 있는지 부터 확인한다. (사실상 이게 첫 번째 읽기)
        bool check = check_file_for_errors(filename);

        if (check == true)
        {
            printf("Syntax Error!!\n");
            continue;
        }

        // 첫번째 읽기. 레이블 값을 추출한다.
        bool first_pass_check = firstPass(filename);
        if (first_pass_check == true)
        {
            printf("Syntax Error!!\n");
            continue;
        }

        // 두 번째 읽기. 이진수 값을 .o파일에 만든다.
        check = processFile(filename);
        if (check == false)
        {
            printf("Syntax Error!!\n");
            continue;
        }

        // 트레이스 파일을 만들자.
        traceFile(filename);

        // 파일이 끝났으니까 초기화를 해준다.
        free(labels);
        labels = NULL;
        free(instructions);
        instructions = NULL;
        free(trace);
        trace = NULL;

        freeMemory();
    }

    return 0;
}

