#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#define RTYPE_OP 0b00000000
#define LW_OP 0b00100011
#define SW_OP 0b00101011
#define ADDI_OP 0b00001000
#define BEQ_OP 0b00000100
#define BNE_OP 0b00000101
#define J_OP 0b00000010
#define JAL_OP 0b00000011

#define JR_FUNCT 0b00001000
#define SLT_FUNCT 0b00101010
#define SLL_FUNCT 0b00000000

#define IMEM_SIZE 32
#define DMEM_SIZE 50
#define MISP_REG_MAX 32

typedef struct {
    int next;
    uint32_t inst;
} IFIDReg;

typedef struct {
    int next;
    int rsCont;
    int rtCont;
    uint32_t signExtend;
    uint32_t jumpAddr;
    int rs;
    int rt;
    unsigned char rd1; 
    unsigned char rd2; 
    unsigned char shamt;

    // Signals
    int RegWrite;
    int ALUSrc;
    int AluOp;
    int RegDst;
    int MemWrite;
    int MemRead;
    int MemToReg;
    int branchSig;
    int branchDiffSig;
    int jumpSig;
    int jalSig;
    int jrSig;
} IDEXReg;

typedef struct {
    int next;
    uint32_t branchAddress;
    int ALURes;
    int rtCont;
    unsigned char rd;

    // Signals
    int RegWrite;
    int zero;
    int branchSig;
    int MemWrite;
    int MemRead;
    int MemToReg;
    int branchDiffSig;
    int jumpSig;
    int jalSig;
    int jrSig;
} EXMEMReg;

typedef struct {
    int ALURes;
    int readData;
    unsigned char rd;

    // Signals
    int RegWrite;
    int MemToReg;
    int jalSig;
} MEMWBReg;

uint32_t IMem[IMEM_SIZE];
IFIDReg fetDecRegLeft = { 0 }, fetDecRegRight = { 0 };
int fileReg[MISP_REG_MAX];
IDEXReg decExeRegLeft = { 0 }, decExeRegRight = { 0 };
EXMEMReg exeMemRegLeft = { 0 }, exeMemRegRight = { 0 };
int DMem[DMEM_SIZE];
MEMWBReg memWriteRegLeft = { 0 }, memWriteRegRight = { 0 };

int stallSignal = 0;
int IFIDWrite = 1;
int PCWrite = 1;
int ForwardA = 0;
int ForwardB = 0;

int PC;
int clockCycle = 0;
int first, second; // ALU Inputs

// Para saber qual instrução esta sendo executada em cada estagio
uint32_t instDECLeft, instDECRight;
uint32_t instEXELeft, instEXERight;
uint32_t instMEMLeft, instMEMRight;
uint32_t instWBLeft, instWBRight;

void outputMemory(FILE *out){
    fprintf(out, "$$$$$$$$$$\nImprimindo DMem\n");
    for(int i=0; i<DMEM_SIZE; i++)
        fprintf(out, "%i -> %d\n", i, DMem[i]);
    fprintf(out, "$$$$$$$$$$\n");
}

void printfileReg(){
    printf("***********\nImprimindo fileReg\n");
    for(int i=0; i<MISP_REG_MAX; i++)
        printf("%i -> %d\n", i, fileReg[i]);
    printf("***********\n");
}

void printSigFetchStage(FILE *out){
    fprintf(out, "Sinais em fetch stage\n");
    fprintf(out, "PCWrite: %d\n", PCWrite);
    fprintf(out, "IFIDWrite: %d\n", IFIDWrite);
}

void printSigDecStage(FILE *out){
    fprintf(out, "Sinais em decode stage\n");
    fprintf(out, "stallSignal: %d\n", stallSignal);
}

void printSigExeStage(FILE *out){
    fprintf(out, "Sinais em execute stage\n");
    fprintf(out, "RegWrite: %d\n", decExeRegRight.RegWrite);
    fprintf(out, "ALUSrc: %d\n", decExeRegRight.ALUSrc);
    fprintf(out, "AluOp: %d\n", decExeRegRight.AluOp);
    fprintf(out, "RegDst: %d\n", decExeRegRight.RegDst);
    fprintf(out, "MemWrite: %d\n", decExeRegRight.MemWrite);
    fprintf(out, "MemRead: %d\n", decExeRegRight.MemRead);
    fprintf(out, "MemToReg: %d\n", decExeRegRight.MemToReg);
    fprintf(out, "branchSig: %d\n", decExeRegRight.branchSig);
    fprintf(out, "branchDiffSig: %d\n", decExeRegRight.branchDiffSig);
    fprintf(out, "jumpSig: %d\n", decExeRegRight.jumpSig);
    fprintf(out, "jalSig: %d\n", decExeRegRight.jalSig);
    fprintf(out, "jrSig: %d\n", decExeRegRight.jrSig);
    fprintf(out, "ForwardA: %d\n", ForwardA);
    fprintf(out, "ForwardB: %d\n", ForwardB);
}

void printSigMemStage(FILE *out){
    fprintf(out, "Sinais em memory stage\n");
    fprintf(out, "RegWrite: %d\n", exeMemRegRight.RegWrite);
    fprintf(out, "zero: %d\n", exeMemRegRight.zero);
    fprintf(out, "branchSig: %d\n", exeMemRegRight.branchSig);
    fprintf(out, "MemWrite: %d\n", exeMemRegRight.MemWrite);
    fprintf(out, "MemRead: %d\n", exeMemRegRight.MemRead);
    fprintf(out, "MemToReg: %d\n", exeMemRegRight.MemToReg);
    fprintf(out, "branchDiffSig: %d\n", exeMemRegRight.branchDiffSig);
    fprintf(out, "jumpSig: %d\n", exeMemRegRight.jumpSig);
    fprintf(out, "jalSig: %d\n", exeMemRegRight.jalSig);
    fprintf(out, "jrSig: %d\n", exeMemRegRight.jrSig);
}

void printSigWBStage(FILE *out){
    fprintf(out, "Sinais em writeback stage\n");
    fprintf(out, "RegWrite: %d\n", memWriteRegRight.RegWrite);
    fprintf(out, "RegWrite: %d\n", memWriteRegRight.MemToReg);
    fprintf(out, "RegWrite: %d\n", memWriteRegRight.jalSig);
}

uint32_t binaryToDecimal(char *bin){
    uint32_t dec = 0, res;
    int index = 0;

    for(int i=31; i>=0; i--){
        res = (bin[i] - '0') * pow(2, index++);
        dec += res;
    }

    return dec;
}

void storeWord(uint32_t word, int *index){
    if(*index >= IMEM_SIZE){
        printf("Memória de instruções lotada\n");
        return;
    }

    printf("Armazenando %u\n", word);
    IMem[(*index)++] = word;
}

int readInstructions(FILE *fp){
    char buffer[33] = {0};
    int countInst = 0;

    if(fp){
        printf("Lendo input via arquivo\n");
        while(fscanf(fp, "%s", buffer) != EOF)
            storeWord(binaryToDecimal(buffer), &countInst);
    }
    else{
        printf("Digite as instruções\n");
        do{
            scanf("%s", buffer);
            storeWord(binaryToDecimal(buffer), &countInst);
        } while(strcmp(buffer, "run"));
    }
    
    printf("Contagem de instruções: %d\n", countInst);
    return countInst;
}

void disableBranchs(){
    decExeRegLeft.branchSig = 0;
    decExeRegLeft.branchDiffSig = 0;
    decExeRegLeft.jrSig = 0;
    decExeRegLeft.jalSig = 0;
    decExeRegLeft.jumpSig = 0;
}

void stallPipeline(){
    stallSignal = 0;
    IFIDWrite = 0;
    PCWrite = 0;
    decExeRegLeft.RegWrite = 0;
    decExeRegLeft.ALUSrc = 0;
    decExeRegLeft.AluOp = 0;
    decExeRegLeft.RegDst = 0;
    decExeRegLeft.MemWrite = 0;
    decExeRegLeft.MemRead = 0;
    decExeRegLeft.MemToReg = 0;
    decExeRegLeft.branchSig = 0;
    decExeRegLeft.branchDiffSig = 0;
    decExeRegLeft.jumpSig = 0;
    decExeRegLeft.jalSig = 0;
    decExeRegLeft.jrSig = 0;
    disableBranchs();
}

void controlUnit(uint32_t binary){
    if(stallSignal)
        stallPipeline();
    else{

        unsigned char opcode = 0xFF & (binary >> 26), funct = 0x3F & binary;

        // jr (R-type)
        if(!(JR_FUNCT ^ funct)){
            printf("jr instruction\n");
            decExeRegLeft.jrSig = 1;
            decExeRegLeft.jumpSig = 0;
            decExeRegLeft.jalSig = 0;
            decExeRegLeft.branchSig = 0;
            decExeRegLeft.branchDiffSig = 0;
            decExeRegLeft.RegWrite = 0;
            decExeRegLeft.RegDst = 0;
            decExeRegLeft.MemWrite = 0;
        }

        else if(!(RTYPE_OP ^ opcode)){
            printf("any arithmetic r-type instruction\n");
            decExeRegLeft.MemWrite = 0;
            decExeRegLeft.RegWrite = 1;
            decExeRegLeft.RegDst = 1;
            decExeRegLeft.AluOp = 10;
            decExeRegLeft.MemRead = 0;
            decExeRegLeft.MemWrite = 0;
            decExeRegLeft.MemToReg = 0;
            disableBranchs();

            if(!(SLT_FUNCT ^ funct)) printf("slt instruction\n");
            if(!(SLL_FUNCT ^ funct)) printf("sll instruction\n");
        }

        // I-type
        if(opcode > 3){

            // lw
            if(!(LW_OP ^ opcode)){
                printf("lw instruction\n");
                decExeRegLeft.RegDst = 0;
                decExeRegLeft.AluOp = 0;
                decExeRegLeft.ALUSrc = 1;
                decExeRegLeft.MemRead = 1;
                decExeRegLeft.MemWrite = 0;
                decExeRegLeft.RegWrite = 1;
                decExeRegLeft.MemToReg = 1;
                disableBranchs();
            }

            // sw
            if(!(SW_OP ^ opcode)){
                printf("sw instruction\n");
                decExeRegLeft.AluOp = 0;
                decExeRegLeft.ALUSrc = 1;
                decExeRegLeft.MemRead = 0;
                decExeRegLeft.MemWrite = 1;
                decExeRegLeft.RegWrite = 0;
                disableBranchs();
            }

            // addi
            if(!(ADDI_OP ^ opcode)){
                printf("addi instruction\n");
                decExeRegLeft.AluOp = 0;
                decExeRegLeft.ALUSrc = 1;
                decExeRegLeft.RegWrite = 1;
                decExeRegLeft.MemToReg = 0;
                decExeRegLeft.MemWrite = 0;
                decExeRegLeft.RegDst = 0;
                disableBranchs();
            }

            // beq 
            if(!(BEQ_OP ^ opcode)){
                printf("beq instruction\n");
                decExeRegLeft.AluOp = 2;
                decExeRegLeft.ALUSrc = 0;
                decExeRegLeft.branchSig = 1;
                decExeRegLeft.MemRead = 0;
                decExeRegLeft.MemWrite = 0;
                decExeRegLeft.RegWrite = 0;
            }

            // bne
            if(!(BNE_OP ^ opcode)){
                printf("bne instructions\n");
                decExeRegLeft.AluOp = 2;
                decExeRegLeft.ALUSrc = 0;
                decExeRegLeft.branchDiffSig = 1;
                decExeRegLeft.MemRead = 0;
                decExeRegLeft.MemWrite = 0;
                decExeRegLeft.RegWrite = 0;
            }

        }

        // J-type
        if(!(J_OP ^ opcode)){
            printf("j instruction\n");
            decExeRegLeft.jumpSig = 1;
            decExeRegLeft.jrSig = 0;
            decExeRegLeft.jalSig = 0;
            decExeRegLeft.branchSig = 0;
            decExeRegLeft.branchDiffSig = 0;
            decExeRegLeft.MemRead = 0;
            decExeRegLeft.MemWrite = 0;
            decExeRegLeft.RegWrite = 0;
        }

        if(!(JAL_OP ^ opcode)){
            printf("jal instruction\n");
            decExeRegLeft.jalSig = 1;
            decExeRegLeft.jrSig = 0;
            decExeRegLeft.jumpSig = 0;
            decExeRegLeft.branchSig = 0;
            decExeRegLeft.branchDiffSig = 0;
            decExeRegLeft.MemRead = 0;
            decExeRegLeft.MemWrite = 0;
            decExeRegLeft.RegWrite = 1;
        }

        printf("\n");
    }
}

void writeBackStage(){
    printf("----------------------------------\n");
    printf("WriteBack stage: %u\n", instWBRight);

    int writeData = memWriteRegRight.MemToReg ? memWriteRegRight.readData : memWriteRegRight.ALURes;

    if(memWriteRegRight.RegWrite)
        if(memWriteRegRight.jalSig)
            fileReg[31] = writeData;
        else
            fileReg[memWriteRegRight.rd] = writeData;
}

void memoryStage(){
    printf("----------------------------------\n");
    printf("Memory stage: %u\n\n", instMEMRight);

    int writeData = exeMemRegRight.rtCont;
    int adress = exeMemRegRight.ALURes / 4;

    if(exeMemRegRight.MemRead)
        memWriteRegLeft.readData = DMem[adress];
    
    if(exeMemRegRight.MemWrite)
        DMem[adress] = writeData;

    memWriteRegLeft.ALURes = exeMemRegRight.jalSig ? exeMemRegRight.next : exeMemRegRight.ALURes;
    memWriteRegLeft.rd = exeMemRegRight.rd;

    memWriteRegLeft.jalSig = exeMemRegRight.jalSig;
    memWriteRegLeft.MemToReg = exeMemRegRight.MemToReg;
    memWriteRegLeft.RegWrite = exeMemRegRight.RegWrite;

    instWBLeft = instMEMRight;
}

void hazardDetectionUnit(uint32_t inst){
    uint32_t rs = 0x1F & (inst >> 21), rt = 0x1F & (inst >> 16);
    if(((rs == decExeRegRight.rd1) || (rt == decExeRegRight.rd1)))
        stallSignal = 1;
}

void decodeStage(){
    printf("----------------------------------\n");
    printf("Decode Stage: %u\n\n", instDECRight);

    uint32_t inst = fetDecRegRight.inst;
    if(decExeRegRight.MemRead) hazardDetectionUnit(inst);
    controlUnit(inst);

    decExeRegLeft.next = fetDecRegRight.next;
    decExeRegLeft.rsCont = fileReg[0x1F & (inst >> 21)];
    decExeRegLeft.rtCont = fileReg[0x1F & (inst >> 16)];
    decExeRegLeft.rd1 = 0x1F & (inst >> 16);
    decExeRegLeft.rd2 = 0x1F & (inst >> 11);
    decExeRegLeft.shamt = 0x1F & (inst >> 6);
    decExeRegLeft.signExtend = 0x0000FFFF & inst;
    decExeRegLeft.jumpAddr = (((0x03FFFFFF & inst) << 2) + (fetDecRegRight.next << 2)) / 4;
    decExeRegLeft.rs = 0x1F & (inst >> 21);
    decExeRegLeft.rt = 0x1F & (inst >> 16);

    printf("rs: %u\n", 0x1F & (inst >> 21));
    printf("rt: %u\n", 0x1F & (inst >> 16));
    printf("rd1 (rt): %u [20:16]\n", 0x1F & (inst >> 16));
    printf("rd2 (rd): %u [15:11]\n", 0x1F & (inst >> 11));
    printf("shamt: %u [10:6]\n", 0x1F & (inst >> 6));
    printf("signExtend: %u\n", 0x0000FFFF & inst);
    printf("(pc + 1 = %u)\n", decExeRegLeft.next);

    instEXELeft = instDECRight;
}

int ALUControl(){
    switch(decExeRegRight.AluOp){
        case 0: return 0;
        case 10:
        {
            unsigned char funct = 0x3F & decExeRegRight.signExtend;
            switch(funct){
                case 32: return 20;
                case 34: return 21;
                case 36: return 22;
                case 37: return 23;
                case 42: return 24;
                case 0 : return 25;
            }
       }
       case 2: return 7;
    }
}

int firstOperand(){
    if(ForwardA == 1)
        return exeMemRegRight.ALURes;
    else if(ForwardA == 2)
        return (memWriteRegRight.MemToReg ? memWriteRegRight.readData : memWriteRegRight.ALURes);
    else
        return decExeRegRight.rsCont;
}

int secondOperand(){
    if(ForwardB == 1)
        return exeMemRegRight.ALURes;
    else if(ForwardB == 2)
        return (memWriteRegRight.MemToReg ? memWriteRegRight.readData : memWriteRegRight.ALURes);
    else
        return decExeRegRight.rtCont;
}

int ALU(){
    int ALUInput = ALUControl(), r;
    exeMemRegLeft.rtCont = second;
    first = firstOperand();
    second = secondOperand();

    switch(ALUInput){
        case 0:  r = first + decExeRegRight.signExtend;
                 break;
        case 11: r = first - second;
                 break;
        case 20: r = first + second;
                 break;
        case 21: r = first - second;
                 break;
        case 22: r = first & second;
                 break;
        case 23: r = first | second;
                 break;
        case 24: r = (uint32_t) (first - second) >> 31;
                 break;
        case 25: r = second << decExeRegRight.shamt;
                 break;
        case 7:  r = first - second;
                 break;
    }

    return r;
}

uint32_t addBranchAddress(){
    return ((decExeRegRight.signExtend << 2) / 4) + decExeRegRight.next;
}

uint32_t jumpBranchMux(){
    return decExeRegRight.jumpSig ? decExeRegRight.jumpAddr : addBranchAddress();
}

uint32_t obtainBranchAddress(){
    return decExeRegRight.jrSig ? first : jumpBranchMux();
}

void forwardingUnit(){
    ForwardA = ForwardB = 0;

    if((exeMemRegRight.RegWrite)
            && (exeMemRegRight.rd != 0)
            && (exeMemRegRight.rd == decExeRegRight.rs))
        ForwardA = 1; // 10

    if((exeMemRegRight.RegWrite)
            && (exeMemRegRight.rd != 0)
            && (exeMemRegRight.rd == decExeRegRight.rt))
        ForwardB = 1;

    if((memWriteRegRight.RegWrite)
            && (memWriteRegRight.rd != 0)
            && (memWriteRegRight.rd == decExeRegRight.rs))
        ForwardA = 2; // 01

    if((memWriteRegRight.RegWrite)
            && (memWriteRegRight.rd != 0)
            && (memWriteRegRight.rd == decExeRegRight.rt))
        ForwardB = 2;

    if((memWriteRegRight.RegWrite)
            && (memWriteRegRight.rd != 0)
            && !(exeMemRegRight.RegWrite && exeMemRegRight.rd != 0
                && (exeMemRegRight.rd != decExeRegRight.rs))
            && (memWriteRegRight.rd == decExeRegRight.rs))
        ForwardA = 2; // 01

    if((memWriteRegRight.RegWrite)
            && (memWriteRegRight.rd != 0)
            && !(exeMemRegRight.RegWrite && exeMemRegRight.rd != 0
                && (exeMemRegRight.rd != decExeRegRight.rt))
            && (memWriteRegRight.rd == decExeRegRight.rt))
        ForwardB = 2; // 01
}

void executeStage(){
    printf("----------------------------------\n");
    printf("Execute Stage: %u\n", instEXERight);

    forwardingUnit();
    int ALURes = ALU();
    printf("ALURes = %d\n", ALURes);

    exeMemRegLeft.zero = ALURes == 0 ? 1 : 0;
    exeMemRegLeft.rd = decExeRegRight.RegDst ? decExeRegRight.rd2 : decExeRegRight.rd1;

    exeMemRegLeft.ALURes = ALURes;
    exeMemRegLeft.branchAddress = obtainBranchAddress();

    exeMemRegLeft.RegWrite = decExeRegRight.RegWrite;
    exeMemRegLeft.MemRead = decExeRegRight.MemRead;
    exeMemRegLeft.MemWrite = decExeRegRight.MemWrite;
    exeMemRegLeft.MemToReg = decExeRegRight.MemToReg;
    exeMemRegLeft.branchSig = decExeRegRight.branchSig;
    exeMemRegLeft.branchDiffSig = decExeRegRight.branchDiffSig;
    exeMemRegLeft.jumpSig = decExeRegRight.jumpSig;
    exeMemRegLeft.jalSig = decExeRegRight.jalSig;
    exeMemRegLeft.jrSig = decExeRegRight.jrSig;
    exeMemRegLeft.next = decExeRegRight.next;

    instMEMLeft = instEXERight;

    printf("Obtained branch address: %d\n", exeMemRegLeft.branchAddress);
}

void setPC(){
    // Branch logical calculation
    if((exeMemRegRight.zero && exeMemRegRight.branchSig)
            || (!exeMemRegRight.zero && exeMemRegRight.branchDiffSig)
            || exeMemRegRight.jumpSig
            || exeMemRegRight.jalSig
            || exeMemRegRight.jrSig)
        PC = exeMemRegRight.branchAddress;
    else
        PC = PC + 1;
}

bool fetchStage(int qtdInstr){
    printf("----------------------------------\n");
    printf("Fetch stage PC: %d\n", PC);

    if(PC < qtdInstr){
        if(IFIDWrite){
            fetDecRegLeft.next = PC + 1;
            fetDecRegLeft.inst = IMem[PC];
            instDECLeft = IMem[PC];
        }
        
        if(PCWrite)
            setPC();

        printf("Fetched instruction: %u\n\n", fetDecRegLeft.inst);
        printf("Proximo valor de PC: %d\n", PC);
        return true;
    }

    printf("Nothing to be fetched.\n");
    return false;
}

void swapRegsAndInst(){
    fetDecRegRight = fetDecRegLeft;
    decExeRegRight = decExeRegLeft;
    exeMemRegRight = exeMemRegLeft;
    memWriteRegRight = memWriteRegLeft;
    instDECRight = instDECLeft;
    instEXERight = instEXELeft;
    instMEMRight = instMEMLeft;
    instWBRight = instWBLeft;
}

void resetMemory(){
    for(int i=0; i<DMEM_SIZE; i++)
        DMem[i] = 0;
}

void resetRegisters(){
    for(int i=0; i<MISP_REG_MAX; i++)
        fileReg[i] = 0;
}
void hold(){
    char c;

    printf("Resetar memoria (m)\n");
    printf("Resetar registradores (r)\n");
    printf("Continuar (s)\n");

    do scanf(" %c", &c);
    while(c != 's' && c != 'm' && c != 'r');

    if(c == 'm') resetMemory();
    if(c == 'r') resetRegisters();
}

void runPipeline(int qtdInstr, int stepMode, FILE *out){
    int inst;

    while(fetchStage(qtdInstr)){
        printf("Clock cycle: %d\n", clockCycle++);

        printSigFetchStage(out);
        outputMemory(out);

        decodeStage();
        printSigDecStage(out);
        outputMemory(out);
        if(stepMode) hold();

        executeStage();
        printSigExeStage(out);
        outputMemory(out);
        if(stepMode) hold();

        memoryStage();
        printSigMemStage(out);
        outputMemory(out);
        if(stepMode) hold();

        writeBackStage();
        printSigWBStage(out);
        outputMemory(out);
        printfileReg();

        swapRegsAndInst();
        printf("========================================================\n");
    }

    for(int i=0; i<5; i++){
        fetchStage(qtdInstr);
        printSigFetchStage(out);
        outputMemory(out);

        decodeStage();
        outputMemory(out);
        if(stepMode) hold();

        executeStage();
        outputMemory(out);
        if(stepMode) hold();

        memoryStage();
        outputMemory(out);
        if(stepMode) hold();

        writeBackStage();
        outputMemory(out);
        printfileReg();

        swapRegsAndInst();
        printf("========================================================\n");
    }
}

void setupRegisters(){
    resetRegisters();
    resetMemory();
}

int getStepOption(){
    printf("Execução direta (0)\n");
    printf("Modo passo a passo (1)\n");

    int stepMode;
    do scanf("%d", &stepMode);
    while(stepMode != 0 && stepMode != 1);

    return stepMode;
}

int getInputOption(){
    printf("Input via teclado (0)\n");
    printf("Input via arquivo (1)\n");

    int input;
    do scanf("%d", &input);
    while(input != 0 && input != 1);

    return input;
}

FILE *getFile(){
    printf("Digite o caminho para o arquivo de input\n");
    char buffer[256];
    scanf("%s", buffer);

    return fopen(buffer, "r");
}

int main(int argc, char *argv[]){
    FILE *memSigOutput = fopen("sigmemory.txt", "w+");

    setupRegisters();
    if(getInputOption()) runPipeline(readInstructions(getFile()), getStepOption(), memSigOutput);
    else runPipeline(readInstructions(NULL), getStepOption(), memSigOutput);

    return EXIT_SUCCESS;
}
