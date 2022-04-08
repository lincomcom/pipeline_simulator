#include <iostream>
#include <cstdio>
#include"pipeline.h"

using namespace std;

//for reading input
static int* input_i;
static char* input_d;
static int input_size_i = 0, input_size_d = 0;//how many bytes of input_i and input_d
//for memory
static char d_memory[MAX_MEMORY] = {'\0'};// one byte per unit
static int i_memory[MAX_WORDS] = {0};//one word per unit
static int regi[NUM_REGISTERS] = {0}, regi_buff[NUM_REGISTERS] =  {0};//regi[29] is $sp
static int PC = 0, PCSave = 0, num_words_i = 0, num_words_d = 0;

//for tracing and controlling simulator
static int cycle_cnt = 0, halt_flag = 0;
//static int write_to_zero_flag = 0;
//for debugging
bool debuger = false;

//for trace forward
static int fwd_ID_rs_flag = 0, fwd_ID_rt_flag = 0, ID_rs = 0, ID_rt = 0;
static int EX_rs = 0, EX_rt = 0;
static struct{
	char *from;
	int flag;
}fwd_EX_rs = {NULL, 0}, fwd_EX_rt = {NULL, 0}, init_fwd = {NULL, 0};
//for mapping
static char* R_Name[R_NUM] = {
		"ADD", "SUB", "AND", "OR", "XOR", "NOR", "NAND", "SLT",
		"SLL", "SRL", "SRA", "JR"
};
static int R_funct[R_NUM] = {
		0x20, 0x22, 0x24, 0x25, 0x26, 0x27, 0x28, 0x2A,
		0x00, 0x02, 0x03, 0x08
};

static char* I_Name[I_NUM] = {
		"ADDI", "LW", "LH", "LHU", "LB", "LBU", "SW", "SH",
		"SB", "LUI", "ANDI", "ORI", "NORI", "SLTI", "BEQ", "BNE"
};
static int I_Opcode[I_NUM] = {
		0x08, 0x23, 0x21, 0x25, 0x20, 0x24, 0x2B, 0x29,
		0x28, 0x0F, 0x0C, 0x0D, 0x0E, 0x0A, 0x04, 0x05
};

static char* J_Name[J_NUM] = { "J", "JAL" };
static int J_Opcode[J_NUM] = {0x02, 0x03};

typedef struct{
	int IR;
	char type; // R, I, J
	char *name;
	int opcode;
	int rd;
	int rs;
	int rt;
	int shamt;
	int funct;
	int imm;
	int address;
}Instruction;

typedef struct {
	Instruction instruct;
	int Result;//ALU_out and MEM_out
}Pipeline_Regi;
 Pipeline_Regi init = {{0,'\0',"NOP",0,0,0,0,0,0,0,0},0},
               IFIDin, IFIDout = {{0,'\0',"NOP",0,0,0,0,0,0,0,0},0},
               IDEXin, IDEXout = {{0,'\0',"NOP",0,0,0,0,0,0,0,0},0},
               EXDMin, EXDMout = {{0,'\0',"NOP",0,0,0,0,0,0,0,0},0},
               DMWBin, DMWBout = {{0,'\0',"NOP",0,0,0,0,0,0,0,0},0},
                            WB = {{0,'\0',"NOP",0,0,0,0,0,0,0,0},0};

static int Flush_flag = 0, Stall_flag = 0;

void initialize(void)
{
    Flush_flag = Stall_flag = 0;
    EX_rs = EX_rt = ID_rs = ID_rt = fwd_ID_rs_flag = fwd_ID_rt_flag = 0;
    PCSave = regi[0] = regi_buff[0] = 0;
    fwd_EX_rs = fwd_EX_rt = init_fwd;
}
//get the instruction and PC+=4
void InstructionFetch(void)
{
     //check misalignment
    if(0 != PC%4){
        fprintf(stderr, "Illegal test case: iimage misalignment!!\n");
        exit(EXIT_FAILURE);
    }
    //check address overflow
    if(MAX_MEMORY <= PC || 0 > PC){
        fprintf(stderr, "Illegal test case: iimage overflow!!\n");
        exit(EXIT_FAILURE);
    }
    IFIDin = init;
    IFIDin.instruct.IR = i_memory[PC/4];
    IFIDin.instruct.opcode = (IFIDin.instruct.IR >> 26) & 0x3f;
    if(debuger){
        printf("cycle%d Instruction:0x%x\n", cycle_cnt, IFIDin.instruct.IR);
        printf("cycle%d opcode:0x%x\n", cycle_cnt, IFIDin.instruct.opcode);
    }
}
//calculate branch PCsave
void InstructionDecode(void)
{
    int i = 0;
    IDEXin = IFIDout;

	IDEXin.instruct.opcode = (IDEXin.instruct.IR >> 26) & 0x3f;

	if (IDEXin.instruct.opcode == 0x3f) {IDEXin.instruct.name = "HALT"; return;}
	/* distinguish type */
	if (IDEXin.instruct.opcode == 0x00) IDEXin.instruct.type = 'R';
	else if (IDEXin.instruct.opcode == 0x02 || IDEXin.instruct.opcode == 0x03) IDEXin.instruct.type = 'J';
	else IDEXin.instruct.type = 'I';

	switch(IDEXin.instruct.type){
    case 'R':
        //decode
        IDEXin.instruct.funct = (IDEXin.instruct.IR)&0x3f;
        IDEXin.instruct.rs = ((IDEXin.instruct.IR)>>21)&0x1f;
        IDEXin.instruct.rt = ((IDEXin.instruct.IR)>>16)&0x1f;
        IDEXin.instruct.rd = ((IDEXin.instruct.IR)>>11)&0x1f;
        IDEXin.instruct.shamt = ((IDEXin.instruct.IR)>>6)&0x1f;

        switch(IDEXin.instruct.funct){
        case 0x08:
            IDEXin.instruct.rt = IDEXin.instruct.rd =IDEXin.instruct.shamt = 0;
            break;
        case 0x00:
        case 0x02:
        case 0x03:
            IDEXin.instruct.rs = 0;
            break;
        default:
            IDEXin.instruct.shamt = 0;
            break;
        }
        //check NOP
        if (!IDEXin.instruct.rt && !IDEXin.instruct.rd &&
            !IDEXin.instruct.shamt && !IDEXin.instruct.funct) {IDEXin.instruct.name = "NOP"; return;}

        for(i = 0; i < R_NUM; i++)
            if(IDEXin.instruct.funct == R_funct[i]) IDEXin.instruct.name =  R_Name[i];

        //check valid R type

        /* load use hazard*/
        if(IsLoad(IDEXout.instruct.opcode)&&
           (IsDataDependency(IDEXout.instruct.rt, IDEXin.instruct.rs) || IsDataDependency(IDEXout.instruct.rt, IDEXin.instruct.rt)))
        {
                Stall_flag = 1;
                return;
        }
        //jr
        if (0x08 == (IDEXin.instruct.funct)){
        /* control hazard*/
            //check preceding instruction(IDEXout)
                if(IsPreInstrBranchHazard(IDEXin.instruct.rs)) return;

            //check 2nd preceding instruction(EXDMout), if stall, return 1; if forward, return 2;else 0
                if(1 == IsPrePreInstrBranchHazard(IDEXin.instruct.rs, 1)) return;



            if (!fwd_ID_rs_flag) ID_rs = regi_buff[IDEXin.instruct.rs];
            PCSave = ID_rs;
            Flush_flag = 1;
            return;
        }//end of jr
        break;//end of R type

    case 'I':
        //decode
        IDEXin.instruct.rs = ((IDEXin.instruct.IR)>>21)&0x1f;
        IDEXin.instruct.rt = ((IDEXin.instruct.IR)>>16)&0x1f;
        IDEXin.instruct.imm = (((IDEXin.instruct.IR)&0xffff)<<16)>>16;


        if(IDEXin.instruct.opcode == 0x0F) IDEXin.instruct.rs = 0;

        for(i = 0; i < I_NUM; i++)
            if(IDEXin.instruct.opcode == I_Opcode[i]) IDEXin.instruct.name =  I_Name[i];
//check valid I type

        /* load use hazard*/
        if(IsLoad(IDEXout.instruct.opcode))
        {
                //store instruction, source is rt
                if(IsStore(IDEXin.instruct.opcode)){
                    if(IsDataDependency(IDEXout.instruct.rt, IDEXin.instruct.rt)){
                         Stall_flag = 1;
                         return;
                    }
                }
                //other I type, source is rs, besides lui
                if(IsDataDependency(IDEXout.instruct.rt, IDEXin.instruct.rs)){
                         Stall_flag = 1;
                         return;
                    }
        }
        //beq and bne
        if(0x04 == IDEXin.instruct.opcode || 0x05 == IDEXin.instruct.opcode){

            /* control hazard*/
            //check preceding instruction(IDEXout)
            if(IsPreInstrBranchHazard(IDEXin.instruct.rs)) return;
            if(IsPreInstrBranchHazard(IDEXin.instruct.rt)) return;

            //check 2nd preceding instruction(EXDMout), if stall, return 1; if forward, return 2;else 0
            if(1 == IsPrePreInstrBranchHazard(IDEXin.instruct.rs, 1)) return;
            if(1 == IsPrePreInstrBranchHazard(IDEXin.instruct.rt, 0)) return;

            if (!fwd_ID_rs_flag) ID_rs = regi_buff[IDEXin.instruct.rs];
            if (!fwd_ID_rt_flag) ID_rt = regi_buff[IDEXin.instruct.rt];
            switch(IDEXin.instruct.opcode){
                case 0x04: /* BEQ (branch if equal) */

                    if (ID_rs == ID_rt){
                        PCSave = PC + 4*IDEXin.instruct.imm;
                        Flush_flag = 1;
                    }
                    break;
                case 0x05: /* BNE (branch if not equal) */
					if (ID_rs != ID_rt){
                        PCSave = PC + 4*IDEXin.instruct.imm;
                        Flush_flag = 1;
                    }
                    break;
            }
        }//end of beq and bne

        break;//end of I type

    case 'J':
        //decode
         for(i = 0; i < J_NUM; i++)
            if(IDEXin.instruct.opcode == J_Opcode[i]) IDEXin.instruct.name =  J_Name[i];

        IDEXin.instruct.address = (IDEXin.instruct.IR)&0x3ffffff;
        PCSave = ((PC+4) & 0xf0000000) | (IDEXin.instruct.address << 2);
        if (0x03 == IDEXin.instruct.opcode){
            IDEXin.instruct.rs = 31;
            IDEXin.Result = PC;
        }
        Flush_flag = 1;
        break;//end of J type

	}

}
//calculate ADD, SUB, and logical operation
void Execute(void)
{
    int tmp_rs = 0, tmp_rt = 0, mask = 0;
    EXDMin = IDEXout;
    //branch instruction, NOP, HALT, return
    if(0x08 == EXDMin.instruct.funct || 0x04 == EXDMin.instruct.opcode || 0x05 == EXDMin.instruct.opcode ||
       0x02 == EXDMin.instruct.opcode|| 0x03 == EXDMin.instruct.opcode || 0x3F == EXDMin.instruct.opcode ||
       !strcmp("NOP", EXDMin.instruct.name)) return;
    switch(EXDMin.instruct.type){
    case 'R':
        //check EX hazard, rs
        if(!IsEX_DMHazard(EXDMin.instruct.rs, 1))
            IsDM_WBHazard(EXDMin.instruct.rs, 1);//check MEM hazard, if !(EX hazard), rs

        //check EX hazard, rt
        if(!IsEX_DMHazard(EXDMin.instruct.rt, 0))
            IsDM_WBHazard(EXDMin.instruct.rt, 0);//check MEM hazard, if !(EX hazard), rt

        //ALU work
        if (!fwd_EX_rs.flag) EX_rs = regi_buff[EXDMin.instruct.rs];
        if (!fwd_EX_rt.flag) EX_rt = regi_buff[EXDMin.instruct.rt];


        switch(EXDMin.instruct.funct){
            case 0x20://add
                tmp_rs = EX_rs;
                tmp_rt = EX_rt;
                EXDMin.Result = EX_rs + EX_rt;

                //Number overflow, P+P=N or N+N=P
                if(IsNumberOverflow(EXDMin.Result, tmp_rs, tmp_rt))
                    PrintError(2);

                break;
            case 0x22://sub
                tmp_rs = EX_rs;
                tmp_rt = EX_rt;
                EXDMin.Result = EX_rs - EX_rt;

                if(IsNumberOverflow(EXDMin.Result, tmp_rs, (-1)*tmp_rt))
                    PrintError(2);
                break;
            case 0x24://and
                EXDMin.Result = EX_rs & EX_rt;
                break;
            case 0x25://or
                EXDMin.Result = EX_rs | EX_rt;
                break;
            case 0x26://xor
                EXDMin.Result = EX_rs ^ EX_rt;
                break;
            case 0x27://nor
                EXDMin.Result = ~(EX_rs | EX_rt);
                break;
            case 0x28://nand
                EXDMin.Result = ~(EX_rs & EX_rt);
                break;
            case 0x2a://slt
                EXDMin.Result = (EX_rs < EX_rt);
                break;
            case 0x00://sll
                EXDMin.Result = EX_rt << EXDMin.instruct.shamt;
                break;
            case 0x02://srl(shift right logical)
                /*
                shift right, fill empty with zero
                  eg. 0x12345678>>8 = 0x00123456
                      0x01234567>>8 = 0x00012345
                */
                if(EXDMin.instruct.shamt == 0) mask = 0xffffffff;
                else mask = ~(0xffffffff << (32-EXDMin.instruct.shamt));
                EXDMin.Result = (EX_rt >> EXDMin.instruct.shamt) & mask;
                break;
            case 0x03://sra(shift right arithmetic)
                 /*
                 shift right, fill empty with sign-extension
                   eg. 0x12345678>>8 = 0xff123456
                      0x01234567>>8 = 0x00012345
                */
                EXDMin.Result = EX_rt >> EXDMin.instruct.shamt;
                break;
        }//end of ALU work
        break;//end of R type

    case 'I':
         //check EX hazard, rs
        if(!IsEX_DMHazard(EXDMin.instruct.rs, 1))
            IsDM_WBHazard(EXDMin.instruct.rs, 1);//check MEM hazard, if !(EX hazard), rs

         //store instruction rt is source
        if(IsStore(EXDMin.instruct.opcode)){
            //check EX hazard, rs
            if(!IsEX_DMHazard(EXDMin.instruct.rt, 0))
                IsDM_WBHazard(EXDMin.instruct.rt, 0);//check MEM hazard, if !(EX hazard), rs
         }
         //ALU work
        if (!fwd_EX_rs.flag) EX_rs = regi_buff[EXDMin.instruct.rs];

        switch(EXDMin.instruct.opcode){
        case 0x08://addi
        case 0x23://lw
        case 0x21://lh
        case 0x25://lhu
        case 0x20://lb
        case 0x24://lbu
        case 0x2b://sw
        case 0x29://sh
        case 0x28://sb
            tmp_rs = EX_rs;
            EXDMin.Result = EX_rs + EXDMin.instruct.imm;
            //check number overflow
            if(IsNumberOverflow(EXDMin.Result, tmp_rs, EXDMin.instruct.imm))
                PrintError(2);

            break;
        case 0x0f://lui
            EXDMin.Result = EXDMin.instruct.imm << 16;
            break;
        case 0x0c://andi
            EXDMin.Result = EX_rs & (EXDMin.instruct.imm & 0xffff);
            break;
        case 0x0d://ori
            EXDMin.Result = EX_rs | (EXDMin.instruct.imm & 0xffff);
            break;
        case 0x0e://nori
            EXDMin.Result = ~(EX_rs | (EXDMin.instruct.imm & 0xffff));
            break;
        case 0x0a://slti
            EXDMin.Result = EX_rs < EXDMin.instruct.imm ;
            break;
        }//end of ALU work
        break;//end of I type
    }
}
//Access d_memory
void MemoryAccess(void)
{
    DMWBin = EXDMout;
    if(!IsLoad(DMWBin.instruct.opcode) && !IsStore(DMWBin.instruct.opcode))
        return;

    int d_men_addr = DMWBin.Result;
    switch (DMWBin.instruct.opcode){
		case 0x23: /* lw (load word) */
					/* Address overflow */
					if (d_men_addr < 0 || d_men_addr > MAX_MEMORY-4) PrintError(3);
					/* Misalignment error */
					if (d_men_addr % 4 != 0) PrintError(4);
					if (halt_flag) break;

					DMWBin.Result = GetWord(d_memory+d_men_addr);
       /* if(debuger) cout << "load d_memory["<<d_men_addr<<"]="
                         <<(int)(d_memory+d_men_addr)<<"to $r"<<DMWBin.instruct.rt<<endl;*/
					break;
        case 0x21: /* lh (load half) */
					/* Address overflow */
					if (d_men_addr < 0 || d_men_addr > MAX_MEMORY-2) PrintError(3);
					/* Misalignment error */
					if (d_men_addr % 2 != 0) PrintError(4);
					if (halt_flag) break;

					DMWBin.Result = GetHalf(d_memory+d_men_addr, 1);
					break;
        case 0x25: /* lhu (load half unsigned) */
					/* Address overflow */
					if (d_men_addr < 0 || d_men_addr > MAX_MEMORY-2) PrintError(3);
					/* Misalignment error */
					if (d_men_addr % 2 != 0) PrintError(4);
					if (halt_flag) break;

					DMWBin.Result = GetHalf(d_memory+d_men_addr, 0);
					break;
        case 0x20: /* lb (load byte) */
					/* Address overflow */
					if (d_men_addr < 0 || d_men_addr > MAX_MEMORY-1) PrintError(3);

					if (halt_flag) break;

					DMWBin.Result = GetByte(d_memory+d_men_addr,1);
					break;
        case 0x24: /* lbu (load byte unsigned) */
					/* Address overflow */
					if (d_men_addr < 0 || d_men_addr > MAX_MEMORY-1) PrintError(3);

					if (halt_flag) break;

					DMWBin.Result = GetByte(d_memory+d_men_addr,0);
					break;
        case 0x2B: /* sw (store word) */
					/* Address overflow */
					if (d_men_addr < 0 || d_men_addr > MAX_MEMORY-4) PrintError(3);
					/* Misalignment error */
					if (d_men_addr % 4 != 0) PrintError(4);
					if (halt_flag) break;

					StoreWord(d_memory+d_men_addr, regi_buff[DMWBin.instruct.rt]);
        if(debuger) cout << "store $r"<<DMWBin.instruct.rt<<":"<<(int)regi_buff[DMWBin.instruct.rt]<<endl;
					break;
        case 0x29: /* sh (store half) */
					/* Address overflow */
					if (d_men_addr < 0 || d_men_addr > MAX_MEMORY-2) PrintError(3);
					/* Misalignment error */
					if (d_men_addr % 2 != 0) PrintError(4);
					if (halt_flag) break;

					StoreHalf(d_memory+d_men_addr, regi_buff[DMWBin.instruct.rt]);
					break;
        case 0x28: /* sb (store byte) */
					/* Address overflow */
					if (d_men_addr < 0 || d_men_addr > MAX_MEMORY-1) PrintError(3);

					if (halt_flag) break;

					StoreByte(d_memory+d_men_addr, regi_buff[DMWBin.instruct.rt]);
					break;
    }

}
//write back to destination register
void WriteBack(void)
{
    WB = DMWBout;
    if(0x3f == WB.instruct.opcode || !strcmp("NOP", WB.instruct.name)) return;
	switch(WB.instruct.type){
		case 'R':
			if (WB.instruct.funct != 0x08 ){
				if (WB.instruct.rd == 0)
					PrintError(1);
				else
					regi_buff[WB.instruct.rd] = WB.Result;
			}
			break;
		case 'I':
			if (WB.instruct.opcode == 0x2B || WB.instruct.opcode == 0x29 ||
				WB.instruct.opcode == 0x28 || WB.instruct.opcode == 0x04 ||
				WB.instruct.opcode == 0x05 ) return;
			else{
				if (WB.instruct.rt == 0)  PrintError(1); // write to $0
				else regi_buff[WB.instruct.rt] = WB.Result;
			}
			break;
		case 'J':
			if (!strcmp(WB.instruct.name, "JAL"))
					regi_buff[31] = WB.Result;
			break;
	}
}
bool IsLoad(int op)
{
    bool ans = false;
    if (op == 0x23 || op == 0x21 || op == 0x25 ||
		op == 0x20 || op == 0x24 ) ans = true;

    return ans;
}
bool IsStore(int op)
{
    bool ans = false;
    if (op == 0x2B || op == 0x29 || op == 0x28) ans = true;

    return ans;
}
bool IsDataDependency(int r_destination, int r_source)
{
    bool ans = false;
    if (0 != r_destination && r_destination == r_source) ans = true;

    return ans;
}
bool IsALU(int op, int funct)
{
    bool ans = true;

    if ( (0x00 == op && 0x08 == funct) ||
         IsLoad(op) || IsStore(op) ||
         0x04 == op || 0x05 == op  ||
         0x02 == op || 0x03 == op  || 0x3F == op) ans = false;

    return ans;
}
//check number overflow
bool IsNumberOverflow(int sum, int addend_1, int addend_2)
{
    bool ans = false;
    int sign_bit_sum = 0, sign_bit_addend_1 = 0, sign_bit_addend_2 = 0;

    sign_bit_sum = (sum>>31) & 0x1;
    sign_bit_addend_1 = (addend_1>>31) & 0x1;
    sign_bit_addend_2 = (addend_2>>31) & 0x1;

    if(sign_bit_addend_1 == sign_bit_addend_2 &&
       sign_bit_addend_1 != sign_bit_sum)
        ans = true;

    return ans;
}
//check preceding instruction(IDEXout)
bool IsPreInstrBranchHazard(int r_source)
{
                bool ans = false;
                //Load instruction, stall
                //ALU instruction, stall
                if(IsLoad(IDEXout.instruct.opcode) || IsALU(IDEXout.instruct.opcode, IDEXout.instruct.funct)){
                    //check data dependency
                    switch(IDEXout.instruct.type){
                    case 'R':
                        if(IsDataDependency(IDEXout.instruct.rd, r_source)){
                            Stall_flag = 1;
                            ans = true;
                        }
                        break;
                    case 'I':
                        if(IsDataDependency(IDEXout.instruct.rt, r_source)){
                            Stall_flag = 1;
                            ans = true;
                        }
                        break;

                    }
                }//end of checking preceding instruction(IDEXout)
                return ans;
}
//check 2nd preceding instruction(EXDMout)
int IsPrePreInstrBranchHazard(int r_source, int src_is_rs)
{
                int ans = 0;

                //Load instruction, stall
                if(IsLoad(EXDMout.instruct.opcode)){
                    if(IsDataDependency(EXDMout.instruct.rt, r_source)){
                            Stall_flag = 1;
                            ans = 1;
                        }
                }
                //jal and ALU instruction, forward
                if(0x03 == EXDMout.instruct.opcode || IsALU(EXDMout.instruct.opcode, EXDMout.instruct.funct)){
                    switch(EXDMout.instruct.type){
                    case 'R':
                        if(IsDataDependency(EXDMout.instruct.rd, r_source)){
                            if(src_is_rs){
                                fwd_ID_rs_flag = 1;
                                ID_rs = EXDMout.Result;
                            }else{
                                fwd_ID_rt_flag = 1;
                                ID_rt = EXDMout.Result;
                            }
                            ans = 2;
                        }
                        break;
                    case 'I':
                        if(IsDataDependency(EXDMout.instruct.rt, r_source)){
                            if(src_is_rs){
                                fwd_ID_rs_flag = 1;
                                ID_rs = EXDMout.Result;
                            }else{
                                fwd_ID_rt_flag = 1;
                                ID_rt = EXDMout.Result;
                            }
                            ans = 2;
                        }
                        break;
                    case 'J':
                        if(IsDataDependency(31, r_source)){
                            if(src_is_rs){
                                fwd_ID_rs_flag = 1;
                                ID_rs = EXDMout.Result;
                            }else{
                                fwd_ID_rt_flag = 1;
                                ID_rt = EXDMout.Result;
                            }
                            ans = 2;
                        }
                        break;
                    }
                }//end of checking 2nd preceding instruction(IDEXout)

    return ans;
}
bool IsEX_DMHazard(int r_source, int src_is_rs)
{
    bool ans = false;
    switch (EXDMout.instruct.type){
		case 'R':
            if (IsDataDependency(EXDMout.instruct.rd, r_source)){
                if(src_is_rs){
                    fwd_EX_rs.from = "EX-DM";
				    fwd_EX_rs.flag = 1;
                    EX_rs = EXDMout.Result;
                    ans = true;
                }else{
                    fwd_EX_rt.from = "EX-DM";
				    fwd_EX_rt.flag = 1;
                    EX_rt = EXDMout.Result;
                    ans = true;
                }
            }

            break;
        case 'I':
            if(EXDMout.instruct.opcode == 0x08 || EXDMout.instruct.opcode == 0x0f ||
               EXDMout.instruct.opcode == 0x0c || EXDMout.instruct.opcode == 0x0d ||
               EXDMout.instruct.opcode == 0x0e || EXDMout.instruct.opcode == 0x0a){
                if (IsDataDependency(EXDMout.instruct.rt, r_source)){
                if(src_is_rs){
                    fwd_EX_rs.from = "EX-DM";
				    fwd_EX_rs.flag = 1;
                    EX_rs = EXDMout.Result;
                    ans = true;
                }else{
                    fwd_EX_rt.from = "EX-DM";
				    fwd_EX_rt.flag = 1;
                    EX_rt = EXDMout.Result;
                    ans = true;
                }
                }
            }
            break;

        }
    return ans;
}
void IsDM_WBHazard(int r_source, int src_is_rs)
{
    switch (DMWBout.instruct.type){
        case 'R':
			if (IsDataDependency(DMWBout.instruct.rd, r_source)){
                if(src_is_rs){
                    fwd_EX_rs.from = "DM-WB";
				    fwd_EX_rs.flag = 1;
                    EX_rs = DMWBout.Result;

                }else{
                    fwd_EX_rt.from = "DM-WB";
				    fwd_EX_rt.flag = 1;
                    EX_rt = DMWBout.Result;

                }
            }
            break;
        case 'I':
            if(DMWBout.instruct.opcode != 0x2B && DMWBout.instruct.opcode != 0x29 &&
               DMWBout.instruct.opcode != 0x28 && DMWBout.instruct.opcode != 0x04 &&
               DMWBout.instruct.opcode != 0x05){
                if (IsDataDependency(DMWBout.instruct.rt, r_source)){
                if(src_is_rs){
                    fwd_EX_rs.from = "DM-WB";
				    fwd_EX_rs.flag = 1;
                    EX_rs = DMWBout.Result;

                }else{
                    fwd_EX_rt.from = "DM-WB";
				    fwd_EX_rt.flag = 1;
                    EX_rt = DMWBout.Result;

                }
                }
            }
            break;
        case 'J':
            if(!strcmp(DMWBout.instruct.name, "JAL")){
                if (IsDataDependency(31, r_source)){
					if(src_is_rs){
                        fwd_EX_rs.from = "DM-WB";
				        fwd_EX_rs.flag = 1;
                        EX_rs = DMWBout.Result;
                    }else{
                        fwd_EX_rt.from = "DM-WB";
				        fwd_EX_rt.flag = 1;
                        EX_rt = DMWBout.Result;
                    }
                }
            }
            break;
    }
}
int main()
{
    FILE* pf_e = fopen("error_dump.rpt", "w");
    FILE* pf_s = fopen("snapshot.rpt", "w");
    //read iimage and dimage to input_i and input_d
    ReadInput();

    //allocate all register, PC, i_memory and d_memory
    Allocate_all();

    while(!halt_flag){
        initialize();
        //check cycle count over 500000
        if(cycle_cnt > 500000){
            fprintf(stderr, "Illegal test case: over 500000 cycles!!\n");
            exit(EXIT_FAILURE);
        }


        //write back to destination register
        WriteBack();
        //Access d_memory
        MemoryAccess();
        //calculate ADD, SUB, and logical operation
        Execute();
        //calculate branch PCsave
        InstructionDecode();
        //get the instruction and PC+=4
        InstructionFetch();
        //print the values of all registers and PC, then cycle_cnt++
        Print_all(pf_s);
        if (    WB.instruct.opcode == 0x3f && DMWBin.instruct.opcode == 0x3f &&
            EXDMin.instruct.opcode == 0x3f && IDEXin.instruct.opcode == 0x3f &&
            IFIDin.instruct.opcode == 0x3f ) break;

        //update PC
        if(Stall_flag) PC = PC;
        else if(Flush_flag) PC = PCSave;
        else PC += 4;

        //synchronize pipeline register in to out, and assign regi_buff[] to regi[]
        Synchronize();
    }
    fclose(pf_e);
    fclose(pf_s);
    return 0;
}

void Synchronize(void){

	if (Flush_flag) IFIDin = init;

	if (!Stall_flag){IFIDout = IFIDin;}
	else IDEXin = init;//if stall, IDEXout = IDEXin = init, and IFIDout = IFIDout, PC =PC

	IDEXout = IDEXin;
	EXDMout = EXDMin;
	DMWBout = DMWBin;
	IFIDin = IDEXin = EXDMin = DMWBin = init;
	for (int i = 0 ; i < NUM_REGISTERS ; i++) regi[i] =  regi_buff[i];
}

int is_little_endian()
{
    uint32_t magic = 0x00000001;

    uint8_t black_magic = *(uint8_t *)&magic;
    return black_magic;
}
uint32_t to_big_endian(uint32_t dword)
{
    if (is_little_endian())
    return (((dword >>  0) & 0xff) << 24)
         | (((dword >>  8) & 0xff) << 16)
         | (((dword >> 16) & 0xff) <<  8)
         | (((dword >> 24) & 0xff) <<  0);
    else
         return dword;
}
void ReadInput(void)
{
    int i = 0;
    FILE* pf_i = fopen("iimage.bin", "rb");
    FILE* pf_d = fopen("dimage.bin", "rb");

    //fail to open file
    if(NULL == pf_i){
        cout<<"Failed to open iimage!!"<<endl;
        exit(1);
    }
    if(NULL == pf_d){
        cout<<"Failed to open dimage!!"<<endl;
        exit(1);
    }
    //count how many bytes in both input
    fseek(pf_i, 0, SEEK_END);//move position indicator to the end of stream
    input_size_i = ftell(pf_i);
    fseek(pf_i, 0, SEEK_SET);//move position indicator to the start of stream

    fseek(pf_d, 0, SEEK_END);//move position indicator to the end of stream
    input_size_d = ftell(pf_d);
    fseek(pf_d, 0, SEEK_SET);//move position indicator to the start of stream

    //allocate memory
    input_i = (int*)malloc(input_size_i);
    input_d = (char*)malloc(input_size_d);

    //read input from both files
    fread(input_i, sizeof(int), input_size_i, pf_i);

    fread(input_d, sizeof(char), input_size_d, pf_d);

    for(i=0; i<input_size_i/4; i++)
        input_i[i] = to_big_endian(input_i[i]);

    fclose(pf_i);
    fclose(pf_d);
}

void Allocate_all(void)
{
   int i = 0, j = 0;
   // all registers set to zero
   for(i=0; i<NUM_REGISTERS; i++) regi[i] = 0;
   //initial PC
   PC = input_i[0];

   //get the number of instructions
   num_words_i = input_i[1];


   //check [PC/4+ num_instruct] < MAX_WORDS
   for(i=(PC/4), j=2; j<num_words_i+2; i++, j++)
       i_memory[i] = input_i[j];

   //initial $sp
     regi[29] = regi_buff[29] = GetWord(input_d);


//get the number of words to be load into d_memory
     num_words_d = GetWord(input_d+4);


//check num_words_d < MAX_WORDS

    for(i= 0, j=8; j<num_words_d*4+8; i++, j++)
       d_memory[i] = input_d[j];

    free(input_d);
    free(input_i);
}


void Print_all(FILE* pf_s)
{
    int i = 0;

    //cycle count
    fprintf(pf_s, "cycle %d\n", cycle_cnt);
    //print all registers
    for(i=0; i<NUM_REGISTERS; i++){
        fprintf(pf_s, "$%02d: 0x%08X\n", i, regi[i]);
    }
    //print PC
    fprintf(pf_s, "PC: 0x%08X\n", PC);

    fprintf(pf_s, "IF: 0x%08X", IFIDin.instruct.IR);
	if (Stall_flag) fprintf(pf_s, " to_be_stalled\n");
	else if (Flush_flag) fprintf(pf_s, " to_be_flushed\n");
	else fprintf(pf_s, "\n");

	fprintf(pf_s, "ID: %s", IDEXin.instruct.name);
	if (Stall_flag) fprintf(pf_s, " to_be_stalled\n");
	else if (fwd_ID_rs_flag && fwd_ID_rt_flag)
        fprintf(pf_s, " fwd_EX-DM_rs_$%d fwd_EX-DM_rt_$%d\n", IDEXin.instruct.rs, IDEXin.instruct.rt);
    else if (fwd_ID_rt_flag)
		fprintf(pf_s, " fwd_EX-DM_rt_$%d\n", IDEXin.instruct.rt);
    else if (fwd_ID_rs_flag)
		fprintf(pf_s, " fwd_EX-DM_rs_$%d\n", IDEXin.instruct.rs);
	else fprintf(pf_s, "\n");

	fprintf(pf_s, "EX: %s", EXDMin.instruct.name);
	if (fwd_EX_rs.flag && fwd_EX_rt.flag)
        fprintf(pf_s, " fwd_%s_rs_$%d fwd_%s_rt_$%d\n", fwd_EX_rs.from, EXDMin.instruct.rs, fwd_EX_rt.from, EXDMin.instruct.rt);
    else if (fwd_EX_rt.flag)
		fprintf(pf_s, " fwd_%s_rt_$%d\n", fwd_EX_rt.from, EXDMin.instruct.rt);
    else if (fwd_EX_rs.flag)
		fprintf(pf_s, " fwd_%s_rs_$%d\n", fwd_EX_rs.from, EXDMin.instruct.rs);
    else fprintf(pf_s, "\n");

	fprintf(pf_s, "DM: %s\n", DMWBin.instruct.name);
	fprintf(pf_s, "WB: %s\n\n\n", WB.instruct.name);

    cycle_cnt++;

}


void PrintError(int error)
{
    FILE* pf_e = fopen("error_dump.rpt", "a");
    switch(error){
    case 1://write to register $0
        fprintf(pf_e, "In cycle %d: Write $0 Error\n", cycle_cnt+1);
        break;
    case 2://Number overflow
        fprintf(pf_e, "In cycle %d: Number Overflow\n", cycle_cnt+1);
        break;
    case 3://Memory address overflow
        fprintf(pf_e, "In cycle %d: Address Overflow\n", cycle_cnt+1);
        halt_flag = 1;
        break;
    case 4://Misalignment
        fprintf(pf_e, "In cycle %d: Misalignment Error\n", cycle_cnt+1);
        halt_flag = 1;
        break;
    }
    fclose(pf_e);
}


void StoreWord(char* base, int register_val)
{
    base[0] = (char)((register_val>>24) & 0x000000ff);//MSB
    base[1] = (char)((register_val>>16) & 0x000000ff);
    base[2] = (char)((register_val>> 8) & 0x000000ff);
    base[3] = (char)((register_val    ) & 0x000000ff);//LSB
}

void StoreHalf(char* base, int register_val)
{
    base[0] = (char)((register_val>>8) & 0x000000ff);//MSB
    base[1] = (char)((register_val   ) & 0x000000ff);//MSB
}
void StoreByte(char* base, int register_val)
{
    base[0] = (char)(register_val & 0x000000ff);//MSB
}

int GetWord(char* base)
{
    int ans = 0;
    unsigned char master_byte = *base;
    unsigned char first_byte = *(base+1);
    unsigned char second_byte = *(base+2);
    unsigned char least_byte = *(base+3);


    ans = (master_byte) << 24| (first_byte) << 16|
          (second_byte) << 8 | (least_byte);

    return ans;
}
int GetHalf(char* base, int sign_flag)//sign_flag = 1: sign extension; sign_flag = 0: unsigned
{
    int ans = 0;
    int mask = 0x0000ffff;

    unsigned char master_byte = *(base);
    unsigned char least_byte = *(base+1);


    ans = (((master_byte)<<8 | (least_byte) ) << 16) >> 16;//sign extension

    if(!sign_flag)//unsigned
        ans = ans & mask;

    return ans;
}

int GetByte(char* base, int sign_flag)//sign_flag = 1: sign extension; sign_flag = 0: unsigned
{
    int ans = 0;
    int mask = 0x000000ff;

    ans = ((*base) << 24) >> 24;//sign extension

    if(!sign_flag)//unsigned
        ans = ans & mask;

    return ans;
}
