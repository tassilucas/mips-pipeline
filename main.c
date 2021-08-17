#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdint.h>
#include <stdbool.h>

#define RTYPE_OP 0b00000000
#define LW_OP 0b00100011
#define SW_OP 0b00101011
#define ADDI_OP 0b00001000
#define BEQ_OP 0b00000100
#define BNQ_OP 0b00000101
#define J_OP 0b00000010
#define JAL_OP 0b00000011

#define JR_FUNCT 0b00001000
#define SLT_FUNCT 0b00101010
#define SLL_FUNCT 0b00000000

#define IMEM_SIZE 4
#define DMEM_SIZE 100
#define MISP_REG_MAX 32

typedef struct {
    int next;
    uint32_t inst;
} IFIDReg;

typedef struct {
    int next;
    uint32_t rsCont;
    uint32_t rtCont;
    uint32_t signExtend;
    uint32_t jumpAddr;
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
    uint32_t rtCont;
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
    uint32_t readData;
    unsigned char rd;

    // Signals
    int RegWrite;
    int MemToReg;
    int jalSig;
} MEMWBReg;

int PCSrc = 0;
uint32_t IMem[IMEM_SIZE];
IFIDReg fetDecReg;
unsigned char fileReg[MISP_REG_MAX];
IDEXReg decExeReg = { 0 };
EXMEMReg exeMemReg;
uint32_t DMem[DMEM_SIZE];
MEMWBReg memWriteReg;

uint32_t binaryToDecimal(char *bin){
    uint32_t dec = 0, res;
    int index = 0;

    for(int i=31; i>=0; i--){
        res = (bin[i] - '0') * pow(2, index++);
        dec += res;
    }

    return dec;
}

// Indo de most significant -> less significant (para debug)
void printBits(uint32_t word){
    printf("Imprimindo os bits de %u\n", word);

    unsigned int size = sizeof(uint32_t);
    int i;
    for(i = size*8-1; i>=0; i--){
        printf("%u",(word >>i) & 1 );
    }

    printf("\n");
}

void storeWord(uint32_t word, int index){
    if(index >= 4){
        printf("Memória de instruções lotada\n");
        return;
    }

    printf("Armazenando %u\n", word);
    IMem[index] = word;
}

void printIMem(){
    void *ptr = IMem;

    printf("Iniciando impressão de MEMORIA DE INSTRUÇÔES\n");
    for(int i=0; i<IMEM_SIZE; i++){
        printf("address %d -> %u\n", i * 4, *(int *)ptr);
        ptr = ptr + 4;
    }
}

int readInstructions(char *filename){
    FILE *fp = fopen(filename, "r");
    char buffer[33] = {0};
    int countInst = 0;

    if(!fp){
        printf("Error to open %s file\n", filename);
        exit(EXIT_FAILURE);
    }

    while(fscanf(fp, "%s", buffer) != EOF){
        storeWord(binaryToDecimal(buffer), countInst);
        countInst++;
    }
    
    return countInst;
}

// true means we succesufly fetched a new instruction
// otherwise, we return false
bool fetchStage(int qtdInstr, uint32_t *inst, int *pc){
    if(*pc < qtdInstr){
        printf("Instruction adress: %d\n", *pc);
        *inst = IMem[*pc];
        *pc = *pc + 1;

       fetDecReg.next = *pc;
       fetDecReg.inst = *inst;

       printf("Processando instrução: %u\n", *inst);
       return true;
    }

    return false;
}

void controlUnit(uint32_t binary){
    unsigned char opcode = 0xFF & (binary >> 26);
    unsigned char funct = 0x3F & binary;
    printf("OPCODE %u | FUNCT %u\n", opcode, funct);

    // jr (R-type)
    if(!(JR_FUNCT ^ funct)){
        printf("jr instruction\n");
        decExeReg.jrSig = 1;
        decExeReg.jumpSig = 1;
        decExeReg.branchSig = decExeReg.branchDiffSig = 0;
        decExeReg.RegWrite = 0;
        decExeReg.RegDst = 0;
        decExeReg.MemWrite = 0;
    }
    
    // any other R-type
    else if(!(RTYPE_OP ^ opcode)){
        printf("any arithmetic r-type instruction\n");
        decExeReg.RegWrite = 1;
        decExeReg.RegDst = 1;
        decExeReg.AluOp = 10;

        if(!(SLT_FUNCT ^ funct)) printf("slt instruction\n");
        if(!(SLT_FUNCT ^ funct)) printf("sll instruction\n");
    }

    // I-type
    if(opcode > 3){
        decExeReg.AluOp = 0;
        decExeReg.ALUSrc = 1;

        // lw
        if(!(LW_OP ^ opcode)){
            printf("lw instruction\n");
            decExeReg.RegWrite = 1;
            decExeReg.MemToReg = 1;
            decExeReg.MemRead = 1;
            decExeReg.RegDst = 1;
        }

        // sw
        if(!(SW_OP ^ opcode)){
            printf("sw instruction\n");
            decExeReg.RegWrite = 0;
            decExeReg.MemToReg = 0;
            decExeReg.MemWrite = 1;
        }

        // addi
        if(!(ADDI_OP ^ opcode)){
            printf("addi instruction\n");
            decExeReg.RegWrite = 1;
            decExeReg.MemToReg = 0;
            decExeReg.MemWrite = 0;
            decExeReg.RegDst = 0;
        }

        // beq 
        if(!(BEQ_OP ^ opcode)){
            printf("beq instruction\n");
            decExeReg.ALUSrc = 0;
            decExeReg.AluOp = 2;
            decExeReg.branchSig = 1;
        }

        // bnq
        if(!(BNQ_OP ^ opcode)){
            printf("bne instruction");
            decExeReg.ALUSrc = 0;
            decExeReg.AluOp = 3;
            decExeReg.branchDiffSig = 1;
        }
    }

    // J-type
    if(!(J_OP ^ opcode)){
        printf("j instruction\n");
        decExeReg.jumpSig = 1;
        decExeReg.branchSig = 0;
        decExeReg.branchDiffSig = 0;
    }

    if(!(JAL_OP ^ opcode)){
        printf("jal instruction\n");
        decExeReg.jalSig = 1;
        decExeReg.jumpSig = 1;
        decExeReg.branchSig = 0;
        decExeReg.branchDiffSig = 0;
    }
}

void decodeStage(){
    printf("==================================\n");
    printf("Decodificando %u\n", fetDecReg.inst);

    uint32_t inst = fetDecReg.inst;
    controlUnit(inst);

    decExeReg.next = fetDecReg.next;
    decExeReg.rsCont = fileReg[0x1F & (inst >> 21)];
    decExeReg.rtCont = fileReg[0x1F & (inst >> 16)];
    decExeReg.rd1 = 0x1F & (inst >> 16);
    decExeReg.rd2 = 0x1F & (inst >> 11);
    decExeReg.shamt = 0x1F & (inst >> 6);
    decExeReg.signExtend = 0x0000FFFF & inst;
    decExeReg.jumpAddr = (((0x03FFFFFF & inst) << 2) + (fetDecReg.next << 2)) / 4;

    printf("rs: %u\n", 0x1F & (inst >> 21));
    printf("rt: %u\n", 0x1F & (inst >> 16));
    printf("rd1: %u [20:16]\n", 0x1F & (inst >> 16));
    printf("rd2: %u [15:11]\n", 0x1F & (inst >> 11));
    printf("shamt: %u [10:6]\n", 0x1F & (inst >> 6));
    printf("signExtend: %u\n", 0x0000FFFF & inst);
    printf("(pc + 4 = %d) jumpAddr: %u\n", fetDecReg.next, decExeReg.jumpAddr);
}

int ALUControl(){
    switch(decExeReg.AluOp){
        case 0: return 0;
        case 10:
        {
            unsigned char funct = 0x3F & decExeReg.signExtend;
            printf("funct value: %u\n", funct);

            switch(funct){
                case 32: return 20;
                case 34: return 21;
                case 36: return 22;
                case 37: return 23;
                case 42: return 24;
                case 0: return 25;
            }
       }
       case 2: return 7;
    }
}

int ALU(){
    int r;
    int ALUInput = ALUControl();
    printf("ALUInput: %d\n", ALUInput);

    switch(ALUInput){
        case 0:  r = decExeReg.rsCont + decExeReg.signExtend;
                 break;
        case 11: r = decExeReg.rsCont - decExeReg.rtCont;
                 break;
        case 20: r = decExeReg.rsCont + decExeReg.rtCont;
                 break;
        case 21: r = decExeReg.rsCont - decExeReg.rtCont;
                 break;
        case 22: r = decExeReg.rsCont & decExeReg.rtCont;
                 break;
        case 23: r = decExeReg.rsCont | decExeReg.rtCont;
                 break;
        case 24: r = (uint32_t) (decExeReg.rsCont - decExeReg.rtCont) >> 31;
                 break;
        case 25: r = decExeReg.rtCont << decExeReg.shamt;
                 break;
        case 7:  r = decExeReg.rsCont - decExeReg.rtCont;
                 break;
    }

    return r;
}

uint32_t addBranchAddress(){
    printf("branchAddress: %u\n", ((decExeReg.signExtend << 2) / 4) + decExeReg.next);
    return ((decExeReg.signExtend << 2) / 4) + decExeReg.next;
}

uint32_t jumpBranchMux(){
    if(decExeReg.jumpSig)
        return decExeReg.jumpAddr;
    else
        return addBranchAddress();
}

uint32_t obtainBranchAddress(){
    if(decExeReg.jrSig)
        return decExeReg.rsCont;
    else
        return jumpBranchMux();
}

void executeStage(){
    int ALURes = ALU();

    printf("ALURES: %d\n", ALURes);

    if(ALURes == 0)
        exeMemReg.zero = 1;
    else
        exeMemReg.zero = 0;

    if(decExeReg.RegDst)
        exeMemReg.rd = decExeReg.rd2;
    else
        exeMemReg.rd = decExeReg.rd1;

    exeMemReg.ALURes = ALURes;
    exeMemReg.branchAddress = obtainBranchAddress();
    exeMemReg.rtCont = decExeReg.rtCont;

    exeMemReg.RegWrite = decExeReg.RegWrite;
    exeMemReg.MemRead = decExeReg.MemRead;
    exeMemReg.MemWrite = decExeReg.MemWrite;
    exeMemReg.MemToReg = decExeReg.MemToReg;
    exeMemReg.branchSig = decExeReg.branchSig;
    exeMemReg.branchDiffSig = decExeReg.branchDiffSig;
    exeMemReg.jumpSig = decExeReg.jumpSig;
    exeMemReg.jalSig = decExeReg.jalSig;
    exeMemReg.next = decExeReg.next;
}

void memoryStage(uint32_t *pc){
    int writeData = exeMemReg.rtCont;
    int adress = exeMemReg.ALURes / 4;

    // beq e bne
    if((exeMemReg.zero && exeMemReg.branchSig)
            || (!exeMemReg.zero && exeMemReg.branchDiffSig)
            || exeMemReg.jumpSig
            || exeMemReg.jalSig
            || exeMemReg.jrSig)
        PCSrc = 1;
    else
        PCSrc = 0;

    printf("Endereço de desvio escolhido: %u\n", exeMemReg.branchAddress);
    printf("PCSrc: %u\n", PCSrc);

    // lw
    if(exeMemReg.MemRead)
        memWriteReg.readData = DMem[adress];
    
    // sw
    if(exeMemReg.MemWrite)
        DMem[adress] = writeData;

    memWriteReg.ALURes = exeMemReg.jalSig ? exeMemReg.next : exeMemReg.ALURes;
    memWriteReg.rd = exeMemReg.rd;

    memWriteReg.jalSig = exeMemReg.jalSig;
    memWriteReg.MemToReg = exeMemReg.MemToReg;
    memWriteReg.RegWrite = exeMemReg.RegWrite;

    printf("ALUResult after multiplexor: %d\n", memWriteReg.ALURes);
}

void writeBackStage(uint32_t *pc){
    int writeData;

    if(memWriteReg.MemToReg)
        writeData = memWriteReg.readData;
    else
        writeData = memWriteReg.ALURes;

    if(memWriteReg.RegWrite)
        if(memWriteReg.jalSig)
            fileReg[31] = writeData;
        else
            fileReg[memWriteReg.rd] = writeData;

    if(memWriteReg.jalSig)
        fileReg[31] = *pc;
}

void printDMem(){
    printf("Imprimindo DMem\n");
    for(int i=0; i<DMEM_SIZE; i++)
        printf("%i -> %u\n", i, DMem[i]);
}

void printfileReg(){
    printf("Imprimindo fileReg\n");
    for(int i=0; i<MISP_REG_MAX; i++)
        printf("%i -> %u\n", i, fileReg[i]);
}

void setPC(int *pc){
    if(PCSrc){
        printf("new PC: %u\n", exeMemReg.branchAddress);
        *pc = exeMemReg.branchAddress;
    }
    else{
        printf("new PC: PC + 1\n");
        *pc = *pc;
    }
}

void runPipeline(int qtdInstr){
    int pc = 0;
    int inst;

    for(int i=0; i<MISP_REG_MAX; i++)
        fileReg[i] = i*10;

    while(1){
        if(fetchStage(qtdInstr, &inst, &pc)){
            decodeStage();
            executeStage();
            memoryStage(&pc);
            writeBackStage(&pc);
        }
        else break;
        setPC(&pc);
    }
}

int main(){
    runPipeline(readInstructions("input.pipe"));
    
    return EXIT_SUCCESS;
}
