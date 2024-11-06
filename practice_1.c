#include <stdio.h>  // 표준 입출력 함수를 사용하기 위해 포함.
#include <string.h> // 문자열 처리 함수를 사용하기 위해 포함.
#include <stdlib.h> // 표준 라이브러리 함수를 사용하기 위해 포함.
#include <ctype.h>  // 주로 문자가 특정 유형인지 검사하거나 문자의 대소문자를 변환하는 함수들을 포함한다.

#define MAX_LINE_LENGTH 1024

typedef struct
{
    char label[256];
    int address;
} Label;

Label *labels = NULL;  // 라벨 배열
int labelCount = 0;    // 라벨 개수
int labelCapacity = 0; // 라벨 배열 용량

typedef struct
{
    char *line;
    int hasLabel;
} Instruction;

Instruction *instructions = NULL; // 명령어 배열
int instructionCount = 0;         // 명령어 개수
int instructionCapacity = 0;      // 명령어 배열 용량

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

typedef struct
{
    char *instName;
    InstructionType type;
    unsigned int opcode;
    unsigned int funct3;
    unsigned int funct7;
} InstructionInfo;

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
    {"exit", EXIT_TYPE, 0, 0, 0},

    // 배열의 끝을 표시하기 위해 NULL 추가
    {NULL, UNKNOWN_TYPE, 0, 0, 0}};

// 이 함수는 주어진 명령어 줄(`line`)을 분석하여 해당 명령어의 유형(`InstructionType`)을 반환하는 역할을 합니다.
InstructionType identifyInstructionType(char *line)
{
    // 널문자 까지 반복해서 모든 문자를 소문자로 만들어준다.
    for (int i = 0; line[i]; i++)
    {
        line[i] = tolower(line[i]);
    }
    for (int i = 0; instructionTable[i].instName != NULL; i++)
    {
        // strstr함수는 찾고자 하는 문자열이 line안에 있으면 그 위치를 가리키는 포인터를 반환하고
        // 그렇지 않으면 NULL을 반환한다.
        if (strstr(line, instructionTable[i].instName))
        {
            return instructionTable[i].type;
        }
    }
    return UNKNOWN_TYPE;
}

// 레지스터 문자열에서 숫자만 추출하는 함수
unsigned int extractRegisterNumber(char *reg)
{
    // 예: "x5"에서 숫자 5를 추출
    if (reg[0] == 'x' || reg[0] == 'X')
    {
        return (unsigned int)atoi(&reg[1]);
    }
    return 0;
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

void processFile(const char *filename)
{
    FILE *inputFile = fopen(filename, "r");
    char outputFilename[260];

    // 파일명.o  파일을 만드는 것
    snprintf(outputFilename, sizeof(outputFilename), "%s.o", strtok(strdup(filename), "."));
    FILE *outputFile = fopen(outputFilename, "w");
    int fileOpenCheck = 1;

    char line[MAX_LINE_LENGTH];
    int pc = 1000;

    if (inputFile == NULL || outputFile == NULL)
    {
        printf("we can't open the file\n");
        return;
    }
    // 파일 끝까지 읽는다.
    while (fgets(line, sizeof(line), inputFile) != NULL)
    {
        // 명령어의 유형을 판별
        InstructionType type = identifyInstructionType(line);

        // 문법에 맞지 않는 assembly 코드가 하나라도 존재하는 경우에
        // Syntax Error을 출력하고 새로운 파일 입력을 대기함. 파일명.o ,파일명.trace파일을 생성하지 않음
        if (type == UNKNOWN_TYPE)
        {
            printf("Syntax Error!!\n");
            fileOpenCheck = 0;
            break;
        }

        // 명령어 정보를 찾아서 이진수로 변환

        unsigned int  instruction ,opcode, funct3, funct7, rd, rs1, rs2, imm;
        char reg1[10], reg2[10], reg3[10];

        
        // 이 부분 for문 손보고 다른 명령어도 제대로 받게 해야한다.
       // 명령어 정보를 찾아서 이진수로 변환
        for (int i = 0; instructionTable[i].instName != NULL; i++) {
            if (strstr(line, instructionTable[i].instName) == line) {
                unsigned int opcode = instructionTable[i].opcode;
                unsigned int funct3 = instructionTable[i].funct3;
                unsigned int funct7 = instructionTable[i].funct7;
                unsigned int rd, rs1, rs2, imm;
                char reg1[10], reg2[10], reg3[10];

                // R-type 명령어 처리
                if (type == R_TYPE) {
                    // R-type 형식: opcode (7 bits) | rd (5 bits) | funct3 (3 bits) | rs1 (5 bits) | rs2 (5 bits) | funct7 (7 bits)
                    sscanf(line, "%*s %s %s %s", reg1, reg2, reg3);
                    rd = extractRegisterNumber(reg1);
                    rs1 = extractRegisterNumber(reg2);
                    rs2 = extractRegisterNumber(reg3);
                    unsigned int instruction = (funct7 << 25) | (rs2 << 20) | (rs1 << 15) | (funct3 << 12) | (rd << 7) | opcode;
                   writeBinary(outputFile, instruction);
                }
                // I-type 명령어 처리
                else if (type == I_TYPE) {
                    // I-type 형식: opcode (7 bits) | rd (5 bits) | funct3 (3 bits) | rs1 (5 bits) | imm (12 bits)
                    sscanf(line, "%*s %s %s %d", reg1, reg2, &imm);
                    rd = extractRegisterNumber(reg1);
                    rs1 = extractRegisterNumber(reg2);
                    imm = (imm & 0xFFF); // sign-extension 처리
                    unsigned int instruction = (imm << 20) | (rs1 << 15) | (funct3 << 12) | (rd << 7) | opcode;
                    writeBinary(outputFile, instruction);
                }
                // S-type 명령어 처리
                else if (type == S_TYPE) {
                    // S-type 형식: opcode (7 bits) | imm[11:5] (7 bits) | rs2 (5 bits) | rs1 (5 bits) | funct3 (3 bits) | imm[4:0] (5 bits)
                    sscanf(line, "%*s %s %s %d", reg1, reg2, &imm);
                    rs1 = extractRegisterNumber(reg1);
                    rs2 = extractRegisterNumber(reg2);
                    unsigned int imm11_5 = (imm >> 5) & 0x7F;
                    unsigned int imm4_0 = imm & 0x1F;
                    unsigned int instruction = (imm11_5 << 25) | (rs2 << 20) | (rs1 << 15) | (funct3 << 12) | (imm4_0 << 7) | opcode;
                    writeBinary(outputFile, instruction);
                }
                // SB-type 명령어 처리
                else if (type == SB_TYPE) {
                    // SB-type 형식: opcode (7 bits) | imm[12|10:5] (7 bits) | rs2 (5 bits) | rs1 (5 bits) | funct3 (3 bits) | imm[4:1|11] (5 bits)
                    sscanf(line, "%*s %s %s %d", reg1, reg2, &imm);
                    rs1 = extractRegisterNumber(reg1);
                    rs2 = extractRegisterNumber(reg2);
                    unsigned int imm12 = (imm >> 11) & 0x1;
                    unsigned int imm10_5 = (imm >> 5) & 0x3F;
                    unsigned int imm4_1 = (imm >> 1) & 0xF;
                    unsigned int imm11 = (imm >> 11) & 0x1;
                    unsigned int instruction = (imm12 << 31) | (imm11 << 7) | (imm10_5 << 25) | (rs2 << 20) | (rs1 << 15) | (funct3 << 12) | (imm4_1 << 8) | opcode;
                    writeBinary(outputFile, instruction);
                }
                // J-type 명령어 처리
                else if (type == J_TYPE) {
                    // J-type 형식: opcode (7 bits) | rd (5 bits) | imm (20 bits)
                    sscanf(line, "%*s %s %d", reg1, &imm);
                    rd = extractRegisterNumber(reg1);
                    unsigned int instruction = ((imm & 0xFFFFF) << 12) | (rd << 7) | opcode;
                    writeBinary(outputFile, instruction);
                }
                break;
            }
        }


        // PC 값 증가 및 출력
        pc += 4;
    }
    if (fileOpenCheck)
    {
        fclose(inputFile);
        fclose(outputFile);
    }
    else
    {
        remove(outputFilename);
    }
}

int main()
{

    char filename[256]; // 입력 파일 이름을 저장할 배열
    FILE *inputFile;

    while (1)
    {
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

        // 파일 처리를 위한 함수 호출
        processFile(filename);
    }

    return 0;
}
