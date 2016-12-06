//==- CGObjCRuntime.cpp - Interface to Shared Objective-C Runtime Features ==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This abstract class defines the interface for Objective-C runtime-specific
// code generation.  It provides some concrete helper methods for functionality
// shared between all (or most) of the Objective-C runtimes supported by clang.
//
//===----------------------------------------------------------------------===//

#include "CGObjCRuntime.h"
#include "CGCleanup.h"
#include "CGRecordLayout.h"
#include "CodeGenFunction.h"
#include "CodeGenModule.h"
#include "clang/AST/RecordLayout.h"
#include "clang/AST/StmtObjC.h"
#include "clang/CodeGen/CGFunctionInfo.h"
#include "llvm/IR/CallSite.h"

using namespace clang;
using namespace CodeGen;

static uint64_t LookupFieldBitOffset(CodeGen::CodeGenModule &CGM,
                                     const ObjCInterfaceDecl *OID,
                                     const ObjCImplementationDecl *ID,
                                     const ObjCIvarDecl *Ivar) {
  const ObjCInterfaceDecl *Container = Ivar->getContainingInterface();

  // FIXME: We should eliminate the need to have ObjCImplementationDecl passed
  // in here; it should never be necessary because that should be the lexical
  // decl context for the ivar.

  // If we know have an implementation (and the ivar is in it) then
  // look up in the implementation layout.
  const ASTRecordLayout *RL;
  if (ID && declaresSameEntity(ID->getClassInterface(), Container))
    RL = &CGM.getContext().getASTObjCImplementationLayout(ID);
  else
    RL = &CGM.getContext().getASTObjCInterfaceLayout(Container);

  // Compute field index.
  //
  // FIXME: The index here is closely tied to how ASTContext::getObjCLayout is
  // implemented. This should be fixed to get the information from the layout
  // directly.
  unsigned Index = 0;

  for (const ObjCIvarDecl *IVD = Container->all_declared_ivar_begin(); 
       IVD; IVD = IVD->getNextIvar()) {
    if (Ivar == IVD)
      break;
    ++Index;
  }
  assert(Index < RL->getFieldCount() && "Ivar is not inside record layout!");

  return RL->getFieldOffset(Index);
}

uint64_t CGObjCRuntime::ComputeIvarBaseOffset(CodeGen::CodeGenModule &CGM,
                                              const ObjCInterfaceDecl *OID,
                                              const ObjCIvarDecl *Ivar) {
  return LookupFieldBitOffset(CGM, OID, nullptr, Ivar) /
    CGM.getContext().getCharWidth();
}

uint64_t CGObjCRuntime::ComputeIvarBaseOffset(CodeGen::CodeGenModule &CGM,
                                              const ObjCImplementationDecl *OID,
                                              const ObjCIvarDecl *Ivar) {
  return LookupFieldBitOffset(CGM, OID->getClassInterface(), OID, Ivar) / 
    CGM.getContext().getCharWidth();
}

unsigned CGObjCRuntime::ComputeBitfieldBitOffset(
    CodeGen::CodeGenModule &CGM,
    const ObjCInterfaceDecl *ID,
    const ObjCIvarDecl *Ivar) {
  return LookupFieldBitOffset(CGM, ID, ID->getImplementation(), Ivar);
}

LValue CGObjCRuntime::EmitValueForIvarAtOffset(CodeGen::CodeGenFunction &CGF,
                                               const ObjCInterfaceDecl *OID,
                                               llvm::Value *BaseValue,
                                               const ObjCIvarDecl *Ivar,
                                               unsigned CVRQualifiers,
                                               llvm::Value *Offset) {
  // Compute (type*) ( (char *) BaseValue + Offset)
  QualType IvarTy = Ivar->getType().withCVRQualifiers(CVRQualifiers);
  llvm::Type *LTy = CGF.CGM.getTypes().ConvertTypeForMem(IvarTy);
  llvm::Value *V = CGF.Builder.CreateBitCast(BaseValue, CGF.Int8PtrTy);
  V = CGF.Builder.CreateInBoundsGEP(V, Offset, "add.ptr");

  if (!Ivar->isBitField()) {
    V = CGF.Builder.CreateBitCast(V, llvm::PointerType::getUnqual(LTy));
    LValue LV = CGF.MakeNaturalAlignAddrLValue(V, IvarTy);
    return LV;
  }

  // We need to compute an access strategy for this bit-field. We are given the
  // offset to the first byte in the bit-field, the sub-byte offset is taken
  // from the original layout. We reuse the normal bit-field access strategy by
  // treating this as an access to a struct where the bit-field is in byte 0,
  // and adjust the containing type size as appropriate.
  //
  // FIXME: Note that currently we make a very conservative estimate of the
  // alignment of the bit-field, because (a) it is not clear what guarantees the
  // runtime makes us, and (b) we don't have a way to specify that the struct is
  // at an alignment plus offset.
  //
  // Note, there is a subtle invariant here: we can only call this routine on
  // non-synthesized ivars but we may be called for synthesized ivars.  However,
  // a synthesized ivar can never be a bit-field, so this is safe.
  uint64_t FieldBitOffset = LookupFieldBitOffset(CGF.CGM, OID, nullptr, Ivar);
  uint64_t BitOffset = FieldBitOffset % CGF.CGM.getContext().getCharWidth();
  uint64_t AlignmentBits = CGF.CGM.getTarget().getCharAlign();
  uint64_t BitFieldSize = Ivar->getBitWidthValue(CGF.getContext());
  CharUnits StorageSize = CGF.CGM.getContext().toCharUnitsFromBits(
      llvm::alignTo(BitOffset + BitFieldSize, AlignmentBits));
  CharUnits Alignment = CGF.CGM.getContext().toCharUnitsFromBits(AlignmentBits);

  // Allocate a new CGBitFieldInfo object to describe this access.
  //
  // FIXME: This is incredibly wasteful, these should be uniqued or part of some
  // layout object. However, this is blocked on other cleanups to the
  // Objective-C code, so for now we just live with allocating a bunch of these
  // objects.
  CGBitFieldInfo *Info = new (CGF.CGM.getContext()) CGBitFieldInfo(
    CGBitFieldInfo::MakeInfo(CGF.CGM.getTypes(), Ivar, BitOffset, BitFieldSize,
                             CGF.CGM.getContext().toBits(StorageSize),
                             CharUnits::fromQuantity(0)));

  Address Addr(V, Alignment);
  Addr = CGF.Builder.CreateElementBitCast(Addr,
                                   llvm::Type::getIntNTy(CGF.getLLVMContext(),
                                                         Info->StorageSize));
  return LValue::MakeBitfield(Addr, *Info, IvarTy, AlignmentSource::Decl);
}

namespace {
  struct CatchHandler {
    const VarDecl *Variable;
    const Stmt *Body;
    llvm::BasicBlock *Block;
    llvm::Constant *TypeInfo;
  };

  struct CallObjCEndCatch final : EHScopeStack::Cleanup {
    CallObjCEndCatch(bool MightThrow, llvm::Value *Fn)
        : MightThrow(MightThrow), Fn(Fn) {}
    bool MightThrow;
    llvm::Value *Fn;

    void Emit(CodeGenFunction &CGF, Flags flags) override {
      if (MightThrow)
        CGF.EmitRuntimeCallOrInvoke(Fn);
      else
        CGF.EmitNounwindRuntimeCall(Fn);
    }
  };
}


void CGObjCRuntime::EmitTryCatchStmt(CodeGenFunction &CGF,
                                     const ObjCAtTryStmt &S,
                                     llvm::Constant *beginCatchFn,
                                     llvm::Constant *endCatchFn,
                                     llvm::Constant *exceptionRethrowFn) {
  // Jump destination for falling out of catch bodies.
  CodeGenFunction::JumpDest Cont;
  if (S.getNumCatchStmts())
    Cont = CGF.getJumpDestInCurrentScope("eh.cont");

  CodeGenFunction::FinallyInfo FinallyInfo;
  if (const ObjCAtFinallyStmt *Finally = S.getFinallyStmt())
    FinallyInfo.enter(CGF, Finally->getFinallyBody(),
                      beginCatchFn, endCatchFn, exceptionRethrowFn);

  SmallVector<CatchHandler, 8> Handlers;

  // Enter the catch, if there is one.
  if (S.getNumCatchStmts()) {
    for (unsigned I = 0, N = S.getNumCatchStmts(); I != N; ++I) {
      const ObjCAtCatchStmt *CatchStmt = S.getCatchStmt(I);
      const VarDecl *CatchDecl = CatchStmt->getCatchParamDecl();

      Handlers.push_back(CatchHandler());
      CatchHandler &Handler = Handlers.back();
      Handler.Variable = CatchDecl;
      Handler.Body = CatchStmt->getCatchBody();
      Handler.Block = CGF.createBasicBlock("catch");

      // @catch(...) always matches.
      if (!CatchDecl) {
        Handler.TypeInfo = nullptr; // catch-all
        // Don't consider any other catches.
        break;
      }

      Handler.TypeInfo = GetEHType(CatchDecl->getType());
    }

    EHCatchScope *Catch = CGF.EHStack.pushCatch(Handlers.size());
    for (unsigned I = 0, E = Handlers.size(); I != E; ++I)
      Catch->setHandler(I, Handlers[I].TypeInfo, Handlers[I].Block);
  }
  
  // Emit the try body.
  CGF.EmitStmt(S.getTryBody());

  // Leave the try.
  if (S.getNumCatchStmts())
    CGF.popCatchScope();

  // Remember where we were.
  CGBuilderTy::InsertPoint SavedIP = CGF.Builder.saveAndClearIP();

  // Emit the handlers.
  for (unsigned I = 0, E = Handlers.size(); I != E; ++I) {
    CatchHandler &Handler = Handlers[I];

    CGF.EmitBlock(Handler.Block);
    llvm::Value *RawExn = CGF.getExceptionFromSlot();

    // Enter the catch.
    llvm::Value *Exn = RawExn;
    if (beginCatchFn)
      Exn = CGF.EmitNounwindRuntimeCall(beginCatchFn, RawExn, "exn.adjusted");

    CodeGenFunction::LexicalScope cleanups(CGF, Handler.Body->getSourceRange());

    if (endCatchFn) {
      // Add a cleanup to leave the catch.
      bool EndCatchMightThrow = (Handler.Variable == nullptr);

      CGF.EHStack.pushCleanup<CallObjCEndCatch>(NormalAndEHCleanup,
                                                EndCatchMightThrow,
                                                endCatchFn);
    }

    // Bind the catch parameter if it exists.
    if (const VarDecl *CatchParam = Handler.Variable) {
      llvm::Type *CatchType = CGF.ConvertType(CatchParam->getType());
      llvm::Value *CastExn = CGF.Builder.CreateBitCast(Exn, CatchType);

      CGF.EmitAutoVarDecl(*CatchParam);
      EmitInitOfCatchParam(CGF, CastExn, CatchParam);
    }

    CGF.ObjCEHValueStack.push_back(Exn);
    CGF.EmitStmt(Handler.Body);
    CGF.ObjCEHValueStack.pop_back();

    // Leave any cleanups associated with the catch.
    cleanups.ForceCleanup();

    CGF.EmitBranchThroughCleanup(Cont);
  }  

  // Go back to the try-statement fallthrough.
  CGF.Builder.restoreIP(SavedIP);

  // Pop out of the finally.
  if (S.getFinallyStmt())
    FinallyInfo.exit(CGF);

  if (Cont.isValid())
    CGF.EmitBlock(Cont.getBlock());
}

void CGObjCRuntime::EmitInitOfCatchParam(CodeGenFunction &CGF,
                                         llvm::Value *exn,
                                         const VarDecl *paramDecl) {

  Address paramAddr = CGF.GetAddrOfLocalVar(paramDecl);

  switch (paramDecl->getType().getQualifiers().getObjCLifetime()) {
  case Qualifiers::OCL_Strong:
    exn = CGF.EmitARCRetainNonBlock(exn);
    // fallthrough

  case Qualifiers::OCL_None:
  case Qualifiers::OCL_ExplicitNone:
  case Qualifiers::OCL_Autoreleasing:
    CGF.Builder.CreateStore(exn, paramAddr);
    return;

  case Qualifiers::OCL_Weak:
    CGF.EmitARCInitWeak(paramAddr, exn);
    return;
  }
  llvm_unreachable("invalid ownership qualifier");
}

namespace {
  struct CallSyncExit final : EHScopeStack::Cleanup {
    llvm::Value *SyncExitFn;
    llvm::Value *SyncArg;
    CallSyncExit(llvm::Value *SyncExitFn, llvm::Value *SyncArg)
      : SyncExitFn(SyncExitFn), SyncArg(SyncArg) {}

    void Emit(CodeGenFunction &CGF, Flags flags) override {
      CGF.Builder.CreateCall(SyncExitFn, SyncArg)->setDoesNotThrow();
    }
  };
}

void CGObjCRuntime::EmitAtSynchronizedStmt(CodeGenFunction &CGF,
                                           const ObjCAtSynchronizedStmt &S,
                                           llvm::Function *syncEnterFn,
                                           llvm::Function *syncExitFn) {
  CodeGenFunction::RunCleanupsScope cleanups(CGF);

  // Evaluate the lock operand.  This is guaranteed to dominate the
  // ARC release and lock-release cleanups.
  const Expr *lockExpr = S.getSynchExpr();
  llvm::Value *lock;
  if (CGF.getLangOpts().ObjCAutoRefCount) {
    lock = CGF.EmitARCRetainScalarExpr(lockExpr);
    lock = CGF.EmitObjCConsumeObject(lockExpr->getType(), lock);
  } else {
    lock = CGF.EmitScalarExpr(lockExpr);
  }
  lock = CGF.Builder.CreateBitCast(lock, CGF.VoidPtrTy);

  // Acquire the lock.
  CGF.Builder.CreateCall(syncEnterFn, lock)->setDoesNotThrow();

  // Register an all-paths cleanup to release the lock.
  CGF.EHStack.pushCleanup<CallSyncExit>(NormalAndEHCleanup, syncExitFn, lock);

  // Emit the body of the statement.
  CGF.EmitStmt(S.getSynchBody());
}

/// Compute the pointer-to-function type to which a message send
/// should be casted in order to correctly call the given method
/// with the given arguments.
///
/// \param method - may be null
/// \param resultType - the result type to use if there's no method
/// \param callArgs - the actual arguments, including implicit ones
CGObjCRuntime::MessageSendInfo
CGObjCRuntime::getMessageSendInfo(const ObjCMethodDecl *method,
                                  QualType resultType,
                                  CallArgList &callArgs) {
  // If there's a method, use information from that.
  if (method) {
    const CGFunctionInfo &signature =
      CGM.getTypes().arrangeObjCMessageSendSignature(method, callArgs[0].Ty);

    const CGFunctionInfo &signatureForCall =
      CGM.getTypes().arrangeCall(signature, callArgs);

    llvm::PointerType *signatureType =
      CGM.getTypes().GetFunctionType(signature)->getPointerTo();

    return MessageSendInfo(signatureForCall, signatureType);
  }

  // There's no method;  just use a default CC.
  const CGFunctionInfo &argsInfo =
    CGM.getTypes().arrangeUnprototypedObjCMessageSend(resultType, callArgs);

  // Derive the signature to call from that.
  llvm::PointerType *signatureType =
    CGM.getTypes().GetFunctionType(argsInfo)->getPointerTo();
  return MessageSendInfo(argsInfo, signatureType);
}

//
// @mulle-objc@ MetaABI: to generate LLVM method argument list
//   this is done differently in mulle-objc so this little code
//   snippet is placed into the runtime
//
CGObjCRuntimeLifetimeMarker   CGObjCRuntime::GenerateCallArgs( CodeGenFunction &CGF,
                                                               CallArgList &Args,
                                                               const ObjCMethodDecl *method,
                                                               const ObjCMessageExpr *Expr)
{
   CGObjCRuntimeLifetimeMarker  Marker;
   CGF.EmitCallArgs( Args, method, Expr->arguments());
   
   Marker.SizeV = nullptr;
   Marker.Addr  = nullptr;
   return( Marker);
}


CGObjCRuntimeLifetimeMarker   CGObjCRuntime::ConvertToMetaABIArgsIfNeeded( CodeGenFunction &CGF,
                                                                           const ObjCMethodDecl *method,
                                                                           CallArgList &Args)
{
   CGObjCRuntimeLifetimeMarker  Marker;
   
   Marker.SizeV = nullptr;
   Marker.Addr  = nullptr;
   return( Marker);
}


void   CGObjCRuntime::GenerateForwardClass(const ObjCInterfaceDecl *OID)
{
   // do nothing
}

// @mulle-objc@ MetaABI: end


CodeGen::RValue  CGObjCRuntime::EmitFastEnumeratorCall( CodeGen::CodeGenFunction &CGF,
                                                       ReturnValueSlot ReturnSlot,
                                                       QualType ResultType,
                                                       Selector Sel,
                                                       llvm::Value *Receiver,
                                                       llvm::Value *StatePtr,
                                                       QualType StateTy,
                                                       llvm::Value *ItemsPtr,
                                                       QualType ItemsTy,
                                                       llvm::Value *Count,
                                                       QualType CountTy)

{
   return( RValue::get( Receiver));  // bogus, this code is never used
}

