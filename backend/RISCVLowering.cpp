#include "RISCVLowering.hpp"
#include "RISCVMIR.hpp"
#include "RegAlloc.hpp"
RISCVAsmPrinter* asmprinter;
void RISCVModuleLowering::LowerGlobalArgument(Module* m){
    // need file name
    asmprinter = new RISCVAsmPrinter("file", m, ctx);
    asmprinter->printAsmGlobal();
    // assert(0&&"Handled later");
}

bool RISCVModuleLowering::run(Module* m){
    LowerGlobalArgument(m);
    // start lowering function
    RISCVFunctionLowering funclower(ctx);
    auto& funcS=m->GetFuncTion();
    for(auto &func:funcS){
        if(funclower.run(func.get())){
            func->print();
            std::cerr<<"FUNC Lowering failed\n";
        }
    }
    
    ctx.print();
    return false;
}

bool RISCVFunctionLowering::run(Function* m){
    /// @note after isel, all insts will be User with an opcode. Only call, ret is not dealt with after this 
    /// @todo deal with alloca and imm
    /// @todo a scheduler can be added here, before or when emitting code to 3-address code
    /// @note This is destory SSA form to 3-address code with mixture of phy and vir regs
    ctx(ctx.mapping(m)->as<RISCVFunction>());
    RISCVISel isel(ctx);
    isel.run(m);

    // Register Allocation
    RegAllocImpl regalloc(ctx.mapping(m)->as<RISCVFunction>());
    regalloc.RunGCpass();
    return false;
}