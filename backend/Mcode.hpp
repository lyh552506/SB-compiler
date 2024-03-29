#pragma once
#include "../lib/BaseCFG.hpp"
#include "../lib/CFG.hpp"
#include "gir.hpp"
#include "my_stl.hpp"
#include <variant>
class MachineUnit;
class MachineFunction;
class MachineBasicBlock;
class MachineInst : public User {
    protected:
    User* IR;
    MachineBasicBlock* mbb;
    Operand rd;
    Operand rs1;
    Operand rs2;
    std::string opcode;
    public:
    MachineInst(User* IR, MachineBasicBlock* mbb, std::string opcode);
    MachineInst(User* IR, MachineBasicBlock* mbb, std::string opcode, Operand rd);
    MachineInst(User* IR, MachineBasicBlock* mbb, std::string opcode, Operand rd, Operand rs1);
    MachineInst(User* IR, MachineBasicBlock* mbb, std::string opcode, Operand rd, Operand rs1, Operand rs2);
    User* getIR();
    MachineBasicBlock* get_machinebasicblock();
    std::string GetOpcode();
    void SetOpcode(std::string opcode);
    Operand GetRd();
    Operand GetRs1();
    Operand GetRs2();
    void SetRd(Operand rd);
    void SetRs1(Operand rs1);
    void SetRs2(Operand rs2);
    void print();
};

class MachineBasicBlock {
    protected:
    BasicBlock* block;
    MachineFunction* mfuc;
    std::string name;
    std::vector<MachineInst*> minsts;
    public:
    MachineBasicBlock(BasicBlock* block, MachineFunction* parent);
    void set_lable(int func_num, int block_num);
    std::string get_name();
    void set_name(std::string name);
    void addMachineInst(MachineInst* minst);
    std::vector<MachineInst*> getMachineInsts();
    BasicBlock* get_block();
    MachineFunction* get_parent();
    void print_block_lable();
};

class MachineFunction {
    protected:
    Function* func;
    MachineUnit* Munit;
    size_t offset;
    int alloca_num;
    size_t stacksize;
    std::vector<MachineBasicBlock*> mblocks;
    std::map<std::string, size_t> offsetMap;
    std::map<std::string, std::string> lableMap;
    public:
    MachineFunction(Function* func, MachineUnit* Munit);
    void set_offset_map(std::string name, size_t offset);
    void set_lable_map(std::string name, std::string lable);
    void set_alloca_and_num();
    void set_stacksize();

    MachineUnit* get_machineunit();
    void addMachineBasicBlock(MachineBasicBlock* mblock);
    std::vector<MachineBasicBlock*> getMachineBasicBlocks();

    size_t get_offset(std::string name);
    std::string get_lable(std::string name);
    int get_allocanum();
    int get_stacksize();

    void print_func_name();
    void print_stack_frame();
    void print_stack_offset();
    void print_func_end();
};

class MachineUnit {
    protected:
    Module* unit;
    std::vector<MachineFunction*> mfuncs;
    public:
    MachineUnit(Module* unit);
    void addMachineFunction(MachineFunction* mfuncs);
    std::vector<MachineFunction*> getMachineFunctions();
};