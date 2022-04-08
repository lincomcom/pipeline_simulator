#include <iostream>
#include <cstdlib>
#include <stdint.h>
#include <cstring>
#define MAX_WORDS 256
#define MAX_MEMORY 1024
#define NUM_REGISTERS 32
#define LEAST_NEGATIVE_NUM 0x80000000
#define MAX_POSITIVE_NUM   0x7fffffff
#define R_NUM   12
#define J_NUM   2
#define I_NUM   16
//read iimage and dimage to input_i and input_d
void ReadInput(void);
//allocate all register, PC, i_memory and d_memory
void Allocate_all(void);

//print the values of all registers and PC, then cycle_cnt++
void Print_all(FILE* pf_s);
//get the instruction and PC+=4
void InstructionFetch(void);
//calculate branch PCsave
void InstructionDecode(void);
//calculate ADD, SUB, and logical operation
void Execute(void);
//Access d_memory
void MemoryAccess(void);
//write back to destination register
void WriteBack(void);
//print error massage
void PrintError(int error);
//check number overflow
bool IsNumberOverflow(int sum, int addend_1, int addend_2);

//get the word, halfword, byte from m-memory
int GetWord(char* base);
int GetHalf(char* base, int sign_flag);//sign_flag = 1:sign extension; sign_flag = 0:unsigned
int GetByte(char* base, int sign_flag);//sign_flag = 1: sign extension; sign_flag = 0: unsigned
//store the word, halfword, byte to m-memory
void StoreWord(char* base, int register_val);
void StoreHalf(char* base, int register_val);
void StoreByte(char* base, int register_val);
//check is an integer(4 bytes) allocated by little-endian
int is_little_endian();
uint32_t to_big_endian(uint32_t dword);

void initialize(void);
void Synchronize(void);

bool IsLoad(int op);
bool IsStore(int op);
bool IsALU(int op, int funct);
bool IsDataDependency(int r_destination, int r_source);
bool IsPreInstrBranchHazard(int r_source);
int IsPrePreInstrBranchHazard(int r_source, int src_is_rs);//if stall, return 1; if forward, return 2;else 0
bool IsEX_DMHazard(int r_source, int src_is_rs);
void IsDM_WBHazard(int r_source, int src_is_rs);
