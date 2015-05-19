//===------- CGObjCMulleRuntime.cpp - Emit LLVM Code from ASTs for a Module --------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This provides Objective-C code generation targeting the Mulle ObjC runtime.  
// The class in this file generates structures used by the Mulle Objective-C 
// runtime library.  These structures are defined elsewhere :)
// This is a tweaked copy off CGObjCMac.cpp. Because those files are as private
// as possible for some reason, inheritance is difficult and not very much
// future proof.
//
// Then stuff got started thrown out and tweaked to taste. OK this is cargo
// cult programming :)
//===----------------------------------------------------------------------===//

#include "CGObjCRuntime.h"
#include "CGBlocks.h"
#include "CGCleanup.h"
#include "CGRecordLayout.h"
#include "CodeGenFunction.h"
#include "CodeGenModule.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/RecordLayout.h"
#include "clang/AST/StmtObjC.h"
#include "clang/Basic/LangOptions.h"
#include "clang/CodeGen/CGFunctionInfo.h"
#include "clang/Frontend/CodeGenOptions.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/MD5.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdio>

using namespace clang;
using namespace CodeGen;

namespace {
   
   // FIXME: We should find a nicer way to make the labels for metadata, string
   // concatenation is lame.
   
   class ObjCCommonTypesHelper {
   protected:
      llvm::LLVMContext &VMContext;
      
   public:
      // The types of these functions don't really matter because we
      // should always bitcast before calling them.
      
      /// id mulle_objc_object_inline_call (id, SEL, void *)
      ///
      /// The messenger, used for all message sends, except super calls
      ///
      llvm::Constant *getMessageSendFn() const {
         // Add the non-lazy-bind attribute, since objc_msgSend is likely to
         // be called a lot.
         llvm::Type *params[] = { ObjectPtrTy, SelectorIDTy, ParamsPtrTy };
         return
         CGM.CreateRuntimeFunction(llvm::FunctionType::get(ObjectPtrTy,
                                                           params, true),
                                   "mulle_objc_object_inline_call",
                                   llvm::AttributeSet::get(CGM.getLLVMContext(),
                                                           llvm::AttributeSet::FunctionIndex,
                                                           llvm::Attribute::NonLazyBind));
      }
      
      /// id mulle_objc_object_call_class_id (id, SEL, void *, CLASSID)
      ///
      /// The messenger used for super calls
      ///
      llvm::Constant *getMessageSendSuperFn() const {
         llvm::Type *params[] = { ObjectPtrTy, SelectorIDTy, ParamsPtrTy, ClassIDTy };
         return CGM.CreateRuntimeFunction(llvm::FunctionType::get(CGM.VoidTy,
                                                                  params, true),
                                          "mulle_objc_object_call_class_id");
      }
      
      
   protected:
      CodeGen::CodeGenModule &CGM;
      
   public:
      llvm::Type *ShortTy, *IntTy, *LongTy, *LongLongTy;
      llvm::Type *Int8PtrTy, *Int8PtrPtrTy;
      llvm::Type *IvarOffsetVarTy;
      
      /// ObjectPtrTy - LLVM type for object handles (typeof(id))
      llvm::Type *ObjectPtrTy;
      
      /// PtrObjectPtrTy - LLVM type for id *
      llvm::Type *PtrObjectPtrTy;
      
      // mulle specific stuff for the various hashes
      llvm::Type *ParamsPtrTy;
      
      llvm::Type *ClassIDTy;
      llvm::Type *CategoryIDTy;
      llvm::Type *SelectorIDTy;
      llvm::Type *ProtocolIDTy;
      
      llvm::Type *ClassIDPtrTy;
      llvm::Type *CategoryIDTyPtrTy;
      llvm::Type *SelectorIDTyPtrTy;
      llvm::Type *ProtocolIDPtrTy;

   private:
      /// ProtocolPtrTy - LLVM type for external protocol handles
      /// (typeof(Protocol))
      llvm::Type *ExternalProtocolPtrTy;
      
   public:
      llvm::Type *getExternalProtocolPtrTy() {
         if (!ExternalProtocolPtrTy) {
            // FIXME: It would be nice to unify this with the opaque type, so that the
            // IR comes out a bit cleaner.
            CodeGen::CodeGenTypes &Types = CGM.getTypes();
            ASTContext &Ctx = CGM.getContext();
            llvm::Type *T = Types.ConvertType(Ctx.getObjCProtoType());
            ExternalProtocolPtrTy = llvm::PointerType::getUnqual(T);
         }
         
         return ExternalProtocolPtrTy;
      }
      
      // SuperCTy - clang type for struct objc_super.
      QualType SuperCTy;
      // SuperPtrCTy - clang type for struct objc_super *.
      QualType SuperPtrCTy;
      
      /// SuperTy - LLVM type for struct objc_super.
      llvm::StructType *SuperTy;
      /// SuperPtrTy - LLVM type for struct objc_super *.
      llvm::Type *SuperPtrTy;
      
      /// PropertyTy - LLVM type for struct objc_property (struct _prop_t
      /// in GCC parlance).
      llvm::StructType *PropertyTy;
      
      /// PropertyListTy - LLVM type for struct objc_property_list
      /// (_prop_list_t in GCC parlance).
      llvm::StructType *PropertyListTy;
      /// PropertyListPtrTy - LLVM type for struct objc_property_list*.
      llvm::Type *PropertyListPtrTy;
      
      // DefTy - LLVM type for struct objc_method.
      llvm::StructType *MethodTy;
      
      /// CacheTy - LLVM type for struct objc_cache.
      llvm::Type *CacheTy;
      /// CachePtrTy - LLVM type for struct objc_cache *.
      llvm::Type *CachePtrTy;
      
      
      llvm::Constant *getClassFn() const {
         llvm::Type *params[] = { ObjectPtrTy };
         return CGM.CreateRuntimeFunction(llvm::FunctionType::get(ObjectPtrTy,
                                                                  params, true),
                                          "_mulle_objc_object_get_class");
      }
      
      llvm::Constant *getGetRuntimeClassFn() {
         llvm::Type *params[] = { ClassIDTy };
         
         return CGM.CreateRuntimeFunction(llvm::FunctionType::get(ObjectPtrTy,
                                                                  params, true),
                                          "mulle_objc_unfailing_get_class");
      }
      
      llvm::Constant *getGetPropertyFn() {
         CodeGen::CodeGenTypes &Types = CGM.getTypes();
         ASTContext &Ctx = CGM.getContext();
         // id objc_getProperty (id, SEL, ptrdiff_t, bool)
         SmallVector<CanQualType,4> Params;
         CanQualType IdType = Ctx.getCanonicalParamType(Ctx.getObjCIdType());
         CanQualType SelType = Ctx.getCanonicalParamType(Ctx.getObjCSelType());
         Params.push_back(IdType);
         Params.push_back(SelType);
         Params.push_back(Ctx.getPointerDiffType()->getCanonicalTypeUnqualified());
         Params.push_back(Ctx.BoolTy);
         llvm::FunctionType *FTy =
         Types.GetFunctionType(Types.arrangeLLVMFunctionInfo(
                                                             IdType, false, false, Params, FunctionType::ExtInfo(),
                                                             RequiredArgs::All));
         return CGM.CreateRuntimeFunction(FTy, "mulle_objc_class_get_property");
      }
      
      llvm::Constant *getSetPropertyFn() {
         CodeGen::CodeGenTypes &Types = CGM.getTypes();
         ASTContext &Ctx = CGM.getContext();
         // void objc_setProperty (id, SEL, ptrdiff_t, id, bool, bool)
         SmallVector<CanQualType,6> Params;
         CanQualType IdType = Ctx.getCanonicalParamType(Ctx.getObjCIdType());
         CanQualType SelType = Ctx.getCanonicalParamType(Ctx.getObjCSelType());
         Params.push_back(IdType);
         Params.push_back(SelType);
         Params.push_back(Ctx.getPointerDiffType()->getCanonicalTypeUnqualified());
         Params.push_back(IdType);
         Params.push_back(Ctx.BoolTy);
         Params.push_back(Ctx.BoolTy);
         llvm::FunctionType *FTy =
         Types.GetFunctionType(Types.arrangeLLVMFunctionInfo(
                                                             Ctx.VoidTy, false, false, Params, FunctionType::ExtInfo(),
                                                             RequiredArgs::All));
         return CGM.CreateRuntimeFunction(FTy, "mulle_objc_class_set_property");
      }
      
      llvm::Constant *getOptimizedSetPropertyFn(bool atomic, bool copy) {
         CodeGen::CodeGenTypes &Types = CGM.getTypes();
         ASTContext &Ctx = CGM.getContext();
         // void objc_setProperty_atomic(id self, SEL _cmd,
         //                              id newValue, ptrdiff_t offset);
         // void objc_setProperty_nonatomic(id self, SEL _cmd,
         //                                 id newValue, ptrdiff_t offset);
         // void objc_setProperty_atomic_copy(id self, SEL _cmd,
         //                                   id newValue, ptrdiff_t offset);
         // void objc_setProperty_nonatomic_copy(id self, SEL _cmd,
         //                                      id newValue, ptrdiff_t offset);
         
         SmallVector<CanQualType,4> Params;
         CanQualType IdType = Ctx.getCanonicalParamType(Ctx.getObjCIdType());
         CanQualType SelType = Ctx.getCanonicalParamType(Ctx.getObjCSelType());
         Params.push_back(IdType);
         Params.push_back(SelType);
         Params.push_back(IdType);
         Params.push_back(Ctx.getPointerDiffType()->getCanonicalTypeUnqualified());
         llvm::FunctionType *FTy =
         Types.GetFunctionType(Types.arrangeLLVMFunctionInfo(
                                                             Ctx.VoidTy, false, false, Params, FunctionType::ExtInfo(),
                                                             RequiredArgs::All));
         const char *name;
         if (atomic && copy)
            name = "objc_setProperty_atomic_copy";
         else if (atomic && !copy)
            name = "objc_setProperty_atomic";
         else if (!atomic && copy)
            name = "objc_setProperty_nonatomic_copy";
         else
            name = "objc_setProperty_nonatomic";
         
         return CGM.CreateRuntimeFunction(FTy, name);
      }
      
      llvm::Constant *getCopyStructFn() {
         CodeGen::CodeGenTypes &Types = CGM.getTypes();
         ASTContext &Ctx = CGM.getContext();
         // void objc_copyStruct (void *, const void *, size_t, bool, bool)
         SmallVector<CanQualType,5> Params;
         Params.push_back(Ctx.VoidPtrTy);
         Params.push_back(Ctx.VoidPtrTy);
         Params.push_back(Ctx.LongTy);
         Params.push_back(Ctx.BoolTy);
         Params.push_back(Ctx.BoolTy);
         llvm::FunctionType *FTy =
         Types.GetFunctionType(Types.arrangeLLVMFunctionInfo(
                                                             Ctx.VoidTy, false, false, Params, FunctionType::ExtInfo(),
                                                             RequiredArgs::All));
         return CGM.CreateRuntimeFunction(FTy, "objc_copyStruct");
      }
      
      /// This routine declares and returns address of:
      /// void objc_copyCppObjectAtomic(
      ///         void *dest, const void *src,
      ///         void (*copyHelper) (void *dest, const void *source));
      llvm::Constant *getCppAtomicObjectFunction() {
         CodeGen::CodeGenTypes &Types = CGM.getTypes();
         ASTContext &Ctx = CGM.getContext();
         /// void objc_copyCppObjectAtomic(void *dest, const void *src, void *helper);
         SmallVector<CanQualType,3> Params;
         Params.push_back(Ctx.VoidPtrTy);
         Params.push_back(Ctx.VoidPtrTy);
         Params.push_back(Ctx.VoidPtrTy);
         llvm::FunctionType *FTy =
         Types.GetFunctionType(Types.arrangeLLVMFunctionInfo(Ctx.VoidTy, false, false,
                                                             Params,
                                                             FunctionType::ExtInfo(),
                                                             RequiredArgs::All));
         return CGM.CreateRuntimeFunction(FTy, "objc_copyCppObjectAtomic");
      }
      
      llvm::Constant *getEnumerationMutationFn() {
         CodeGen::CodeGenTypes &Types = CGM.getTypes();
         ASTContext &Ctx = CGM.getContext();
         // void objc_enumerationMutation (id)
         SmallVector<CanQualType,1> Params;
         Params.push_back(Ctx.getCanonicalParamType(Ctx.getObjCIdType()));
         llvm::FunctionType *FTy =
         Types.GetFunctionType(Types.arrangeLLVMFunctionInfo(
                                                             Ctx.VoidTy, false, false, Params, FunctionType::ExtInfo(),
                                                             RequiredArgs::All));
         return CGM.CreateRuntimeFunction(FTy, "objc_enumerationMutation");
      }
      
      /// GcReadWeakFn -- LLVM objc_read_weak (id *src) function.
      llvm::Constant *getGcReadWeakFn() {
         // id objc_read_weak (id *)
         llvm::Type *args[] = { ObjectPtrTy->getPointerTo() };
         llvm::FunctionType *FTy =
         llvm::FunctionType::get(ObjectPtrTy, args, false);
         return CGM.CreateRuntimeFunction(FTy, "objc_read_weak");
      }
      
      /// GcAssignWeakFn -- LLVM objc_assign_weak function.
      llvm::Constant *getGcAssignWeakFn() {
         // id objc_assign_weak (id, id *)
         llvm::Type *args[] = { ObjectPtrTy, ObjectPtrTy->getPointerTo() };
         llvm::FunctionType *FTy =
         llvm::FunctionType::get(ObjectPtrTy, args, false);
         return CGM.CreateRuntimeFunction(FTy, "objc_assign_weak");
      }
      
      /// GcAssignGlobalFn -- LLVM objc_assign_global function.
      llvm::Constant *getGcAssignGlobalFn() {
         // id objc_assign_global(id, id *)
         llvm::Type *args[] = { ObjectPtrTy, ObjectPtrTy->getPointerTo() };
         llvm::FunctionType *FTy =
         llvm::FunctionType::get(ObjectPtrTy, args, false);
         return CGM.CreateRuntimeFunction(FTy, "objc_assign_global");
      }
      
      /// GcAssignThreadLocalFn -- LLVM objc_assign_threadlocal function.
      llvm::Constant *getGcAssignThreadLocalFn() {
         // id objc_assign_threadlocal(id src, id * dest)
         llvm::Type *args[] = { ObjectPtrTy, ObjectPtrTy->getPointerTo() };
         llvm::FunctionType *FTy =
         llvm::FunctionType::get(ObjectPtrTy, args, false);
         return CGM.CreateRuntimeFunction(FTy, "objc_assign_threadlocal");
      }
      
      /// GcAssignIvarFn -- LLVM objc_assign_ivar function.
      llvm::Constant *getGcAssignIvarFn() {
         // id objc_assign_ivar(id, id *, ptrdiff_t)
         llvm::Type *args[] = { ObjectPtrTy, ObjectPtrTy->getPointerTo(),
            CGM.PtrDiffTy };
         llvm::FunctionType *FTy =
         llvm::FunctionType::get(ObjectPtrTy, args, false);
         return CGM.CreateRuntimeFunction(FTy, "objc_assign_ivar");
      }
      
      /// GcMemmoveCollectableFn -- LLVM objc_memmove_collectable function.
      llvm::Constant *GcMemmoveCollectableFn() {
         // void *objc_memmove_collectable(void *dst, const void *src, size_t size)
         llvm::Type *args[] = { Int8PtrTy, Int8PtrTy, LongTy };
         llvm::FunctionType *FTy = llvm::FunctionType::get(Int8PtrTy, args, false);
         return CGM.CreateRuntimeFunction(FTy, "objc_memmove_collectable");
      }
      
      /// GcAssignStrongCastFn -- LLVM objc_assign_strongCast function.
      llvm::Constant *getGcAssignStrongCastFn() {
         // id objc_assign_strongCast(id, id *)
         llvm::Type *args[] = { ObjectPtrTy, ObjectPtrTy->getPointerTo() };
         llvm::FunctionType *FTy =
         llvm::FunctionType::get(ObjectPtrTy, args, false);
         return CGM.CreateRuntimeFunction(FTy, "objc_assign_strongCast");
      }
      
      /// ExceptionThrowFn - LLVM objc_exception_throw function.
      llvm::Constant *getExceptionThrowFn() {
         // void objc_exception_throw(id)
         llvm::Type *args[] = { ObjectPtrTy };
         llvm::FunctionType *FTy =
         llvm::FunctionType::get(CGM.VoidTy, args, false);
         return CGM.CreateRuntimeFunction(FTy, "objc_exception_throw");
      }
      
      /// ExceptionRethrowFn - LLVM objc_exception_rethrow function.
      llvm::Constant *getExceptionRethrowFn() {
         // void objc_exception_rethrow(void)
         llvm::FunctionType *FTy = llvm::FunctionType::get(CGM.VoidTy, false);
         return CGM.CreateRuntimeFunction(FTy, "objc_exception_rethrow");
      }
      
      /// SyncEnterFn - LLVM object_sync_enter function.
      llvm::Constant *getSyncEnterFn() {
         // int objc_sync_enter (id)
         llvm::Type *args[] = { ObjectPtrTy };
         llvm::FunctionType *FTy =
         llvm::FunctionType::get(CGM.IntTy, args, false);
         return CGM.CreateRuntimeFunction(FTy, "objc_sync_enter");
      }
      
      /// SyncExitFn - LLVM object_sync_exit function.
      llvm::Constant *getSyncExitFn() {
         // int objc_sync_exit (id)
         llvm::Type *args[] = { ObjectPtrTy };
         llvm::FunctionType *FTy =
         llvm::FunctionType::get(CGM.IntTy, args, false);
         return CGM.CreateRuntimeFunction(FTy, "objc_sync_exit");
      }
      
      ObjCCommonTypesHelper(CodeGen::CodeGenModule &cgm);
      ~ObjCCommonTypesHelper(){}
   };
   
   /// ObjCTypesHelper - Helper class that encapsulates lazy
   /// construction of varies types used during ObjC generation.
   class ObjCTypesHelper : public ObjCCommonTypesHelper {
   public:
      /// SymtabTy - LLVM type for struct objc_symtab.
      llvm::StructType *SymtabTy;
      /// SymtabPtrTy - LLVM type for struct objc_symtab *.
      llvm::Type *SymtabPtrTy;
      /// ModuleTy - LLVM type for struct objc_module.
      llvm::StructType *ModuleTy;
      
      /// ProtocolTy - LLVM type for struct objc_protocol.
      llvm::StructType *ProtocolTy;
      /// ProtocolPtrTy - LLVM type for struct objc_protocol *.
      llvm::Type *ProtocolPtrTy;
      /// ProtocolExtensionTy - LLVM type for struct
      /// objc_protocol_extension.
      llvm::StructType *ProtocolExtensionTy;
      /// ProtocolExtensionTy - LLVM type for struct
      /// objc_protocol_extension *.
      llvm::Type *ProtocolExtensionPtrTy;
      /// MethodDescriptionTy - LLVM type for struct
      /// objc_method_description.
      llvm::StructType *MethodDescriptionTy;
      /// MethodDescriptionListTy - LLVM type for struct
      /// objc_method_description_list.
      llvm::StructType *MethodDescriptionListTy;
      /// MethodDescriptionListPtrTy - LLVM type for struct
      /// objc_method_description_list *.
      llvm::Type *MethodDescriptionListPtrTy;
      /// ProtocolListTy - LLVM type for struct objc_property_list.
      llvm::StructType *ProtocolListTy;
      /// ProtocolListPtrTy - LLVM type for struct objc_property_list*.
      llvm::Type *ProtocolListPtrTy;
      /// CategoryTy - LLVM type for struct objc_category.
      llvm::StructType *CategoryTy;
      /// ClassTy - LLVM type for struct objc_class.
      llvm::StructType *ClassTy;
      /// ClassPtrTy - LLVM type for struct objc_class *.
      llvm::Type *ClassPtrTy;
      /// ClassExtensionTy - LLVM type for struct objc_class_ext.
      llvm::StructType *ClassExtensionTy;
      /// ClassExtensionPtrTy - LLVM type for struct objc_class_ext *.
      llvm::Type *ClassExtensionPtrTy;
      // IvarTy - LLVM type for struct objc_ivar.
      llvm::StructType *IvarTy;
      /// IvarListTy - LLVM type for struct objc_ivar_list.
      llvm::Type *IvarListTy;
      /// IvarListPtrTy - LLVM type for struct objc_ivar_list *.
      llvm::Type *IvarListPtrTy;
      /// MethodListTy - LLVM type for struct objc_method_list.
      llvm::Type *MethodListTy;
      /// MethodListPtrTy - LLVM type for struct objc_method_list *.
      llvm::Type *MethodListPtrTy;
      
      /// ExceptionDataTy - LLVM type for struct _objc_exception_data.
      llvm::Type *ExceptionDataTy;
      
      /// ExceptionTryEnterFn - LLVM objc_exception_try_enter function.
      llvm::Constant *getExceptionTryEnterFn() {
         llvm::Type *params[] = { ExceptionDataTy->getPointerTo() };
         return CGM.CreateRuntimeFunction(
                                          llvm::FunctionType::get(CGM.VoidTy, params, false),
                                          "objc_exception_try_enter");
      }
      
      /// ExceptionTryExitFn - LLVM objc_exception_try_exit function.
      llvm::Constant *getExceptionTryExitFn() {
         llvm::Type *params[] = { ExceptionDataTy->getPointerTo() };
         return CGM.CreateRuntimeFunction(
                                          llvm::FunctionType::get(CGM.VoidTy, params, false),
                                          "objc_exception_try_exit");
      }
      
      /// ExceptionExtractFn - LLVM objc_exception_extract function.
      llvm::Constant *getExceptionExtractFn() {
         llvm::Type *params[] = { ExceptionDataTy->getPointerTo() };
         return CGM.CreateRuntimeFunction(llvm::FunctionType::get(ObjectPtrTy,
                                                                  params, false),
                                          "objc_exception_extract");
      }
      
      /// ExceptionMatchFn - LLVM objc_exception_match function.
      llvm::Constant *getExceptionMatchFn() {
         llvm::Type *params[] = { ClassPtrTy, ObjectPtrTy };
         return CGM.CreateRuntimeFunction(
                                          llvm::FunctionType::get(CGM.Int32Ty, params, false),
                                          "objc_exception_match");
         
      }
      
      /// SetJmpFn - LLVM _setjmp function.
      llvm::Constant *getSetJmpFn() {
         // This is specifically the prototype for x86.
         llvm::Type *params[] = { CGM.Int32Ty->getPointerTo() };
         return
         CGM.CreateRuntimeFunction(llvm::FunctionType::get(CGM.Int32Ty,
                                                           params, false),
                                   "_setjmp",
                                   llvm::AttributeSet::get(CGM.getLLVMContext(),
                                                           llvm::AttributeSet::FunctionIndex,
                                                           llvm::Attribute::NonLazyBind));
      }
      
   public:
      ObjCTypesHelper(CodeGen::CodeGenModule &cgm);
      ~ObjCTypesHelper() {}
   };
   

   /*
    * Biting the hand that feeds, but damn if this files isn't way
    * too long, way to unstructured and all in C++.
    *
    */
   
   class CGObjCCommonMulleRuntime : public CodeGen::CGObjCRuntime {
   public:
      // FIXME - accessibility
      class GC_IVAR {
      public:
         unsigned ivar_bytepos;
         unsigned ivar_size;
         GC_IVAR(unsigned bytepos = 0, unsigned size = 0)
         : ivar_bytepos(bytepos), ivar_size(size) {}
         
         // Allow sorting based on byte pos.
         bool operator<(const GC_IVAR &b) const {
            return ivar_bytepos < b.ivar_bytepos;
         }
      };
      
      class SKIP_SCAN {
      public:
         unsigned skip;
         unsigned scan;
         SKIP_SCAN(unsigned _skip = 0, unsigned _scan = 0)
         : skip(_skip), scan(_scan) {}
      };
      
      /// opcode for captured block variables layout 'instructions'.
      /// In the following descriptions, 'I' is the value of the immediate field.
      /// (field following the opcode).
      ///
      enum BLOCK_LAYOUT_OPCODE {
         /// An operator which affects how the following layout should be
         /// interpreted.
         ///   I == 0: Halt interpretation and treat everything else as
         ///           a non-pointer.  Note that this instruction is equal
         ///           to '\0'.
         ///   I != 0: Currently unused.
         BLOCK_LAYOUT_OPERATOR            = 0,
         
         /// The next I+1 bytes do not contain a value of object pointer type.
         /// Note that this can leave the stream unaligned, meaning that
         /// subsequent word-size instructions do not begin at a multiple of
         /// the pointer size.
         BLOCK_LAYOUT_NON_OBJECT_BYTES    = 1,
         
         /// The next I+1 words do not contain a value of object pointer type.
         /// This is simply an optimized version of BLOCK_LAYOUT_BYTES for
         /// when the required skip quantity is a multiple of the pointer size.
         BLOCK_LAYOUT_NON_OBJECT_WORDS    = 2,
         
         /// The next I+1 words are __strong pointers to Objective-C
         /// objects or blocks.
         BLOCK_LAYOUT_STRONG              = 3,
         
         /// The next I+1 words are pointers to __block variables.
         BLOCK_LAYOUT_BYREF               = 4,
         
         /// The next I+1 words are __weak pointers to Objective-C
         /// objects or blocks.
         BLOCK_LAYOUT_WEAK                = 5,
         
         /// The next I+1 words are __unsafe_unretained pointers to
         /// Objective-C objects or blocks.
         BLOCK_LAYOUT_UNRETAINED          = 6
         
         /// The next I+1 words are block or object pointers with some
         /// as-yet-unspecified ownership semantics.  If we add more
         /// flavors of ownership semantics, values will be taken from
         /// this range.
         ///
         /// This is included so that older tools can at least continue
         /// processing the layout past such things.
         //BLOCK_LAYOUT_OWNERSHIP_UNKNOWN = 7..10,
         
         /// All other opcodes are reserved.  Halt interpretation and
         /// treat everything else as opaque.
      };
      
      class RUN_SKIP {
      public:
         enum BLOCK_LAYOUT_OPCODE opcode;
         CharUnits block_var_bytepos;
         CharUnits block_var_size;
         RUN_SKIP(enum BLOCK_LAYOUT_OPCODE Opcode = BLOCK_LAYOUT_OPERATOR,
                  CharUnits BytePos = CharUnits::Zero(),
                  CharUnits Size = CharUnits::Zero())
         : opcode(Opcode), block_var_bytepos(BytePos),  block_var_size(Size) {}
         
         // Allow sorting based on byte pos.
         bool operator<(const RUN_SKIP &b) const {
            return block_var_bytepos < b.block_var_bytepos;
         }
      };
      
   protected:
      llvm::LLVMContext &VMContext;
      // FIXME! May not be needing this after all.
      unsigned ObjCABI;
      
      // gc ivar layout bitmap calculation helper caches.
      SmallVector<GC_IVAR, 16> SkipIvars;
      SmallVector<GC_IVAR, 16> IvarsInfo;
      
      // arc/mrr layout of captured block literal variables.
      SmallVector<RUN_SKIP, 16> RunSkipBlockVars;
      
      /// LazySymbols - Symbols to generate a lazy reference for. See
      /// DefinedSymbols and FinishModule().
      llvm::SetVector<IdentifierInfo*> LazySymbols;

      /// DefinedSymbols - External symbols which are defined by this
      /// module. The symbols in this list and LazySymbols are used to add
      /// special linker symbols which ensure that Objective-C modules are
      /// linked properly.
      llvm::SetVector<IdentifierInfo*> DefinedSymbols;
      
      /// ClassNames - uniqued class names.
      llvm::StringMap<llvm::GlobalVariable*> ClassNames;
      
      /// MethodVarNames - uniqued method variable names.
      llvm::DenseMap<Selector, llvm::GlobalVariable*> MethodVarNames;
      
      /// DefinedCategoryNames - list of category names in form Class_Category.
      llvm::SetVector<std::string> DefinedCategoryNames;
      
      /// MethodVarTypes - uniqued method type signatures. We have to use
      /// a StringMap here because have no other unique reference.
      llvm::StringMap<llvm::GlobalVariable*> MethodVarTypes;
      
      /// MethodDefinitions - map of methods which have been defined in
      /// this translation unit.
      llvm::DenseMap<const ObjCMethodDecl*, llvm::Function*> MethodDefinitions;
      
      /// PropertyNames - uniqued method variable names.
      llvm::DenseMap<IdentifierInfo*, llvm::GlobalVariable*> PropertyNames;
      
      /// ClassReferences - uniqued class references.
      llvm::DenseMap<IdentifierInfo*, llvm::GlobalVariable*> ClassReferences;
      
      /// SelectorReferences - uniqued selector references.
      llvm::DenseMap<Selector, llvm::GlobalVariable*> SelectorReferences;
      
      /// Protocols - Protocols for which an objc_protocol structure has
      /// been emitted. Forward declarations are handled by creating an
      /// empty structure whose initializer is filled in when/if defined.
      llvm::DenseMap<IdentifierInfo*, llvm::GlobalVariable*> Protocols;
      
      /// DefinedProtocols - Protocols which have actually been
      /// defined. We should not need this, see FIXME in GenerateProtocol.
      llvm::DenseSet<IdentifierInfo*> DefinedProtocols;
      
      /// DefinedClasses - List of defined classes.
      SmallVector<llvm::GlobalValue*, 16> DefinedClasses;
      
      /// ImplementedClasses - List of @implemented classes.
      SmallVector<const ObjCInterfaceDecl*, 16> ImplementedClasses;
      
      /// DefinedNonLazyClasses - List of defined "non-lazy" classes.
      SmallVector<llvm::GlobalValue*, 16> DefinedNonLazyClasses;
      
      /// DefinedCategories - List of defined categories.
      SmallVector<llvm::GlobalValue*, 16> DefinedCategories;
      
      /// DefinedNonLazyCategories - List of defined "non-lazy" categories.
      SmallVector<llvm::GlobalValue*, 16> DefinedNonLazyCategories;
      
      /// GetNameForMethod - Return a name for the given method.
      /// \param[out] NameOut - The return value.
      void GetNameForMethod(const ObjCMethodDecl *OMD,
                            const ObjCContainerDecl *CD,
                            SmallVectorImpl<char> &NameOut);
      
      /// GetMethodVarName - Return a unique constant for the given
      /// selector's name. The return value has type char *.
      llvm::Constant *GetMethodVarName(Selector Sel);
      llvm::Constant *GetMethodVarName(IdentifierInfo *Ident);
      
      /// GetMethodVarType - Return a unique constant for the given
      /// method's type encoding string. The return value has type char *.
      
      // FIXME: This is a horrible name.
      llvm::Constant *GetMethodVarType(const ObjCMethodDecl *D,
                                       bool Extended = false);
      llvm::Constant *GetMethodVarType(const FieldDecl *D);
      
      /// GetPropertyName - Return a unique constant for the given
      /// name. The return value has type char *.
      llvm::Constant *GetPropertyName(IdentifierInfo *Ident);
      
      // FIXME: This can be dropped once string functions are unified.
      llvm::Constant *GetPropertyTypeString(const ObjCPropertyDecl *PD,
                                            const Decl *Container);
      
      /// GetClassName - Return a unique constant for the given selector's
      /// runtime name (which may change via use of objc_runtime_name attribute on
      /// class or protocol definition. The return value has type char *.
      llvm::Constant *GetClassName(StringRef RuntimeName);
      
      llvm::Function *GetMethodDefinition(const ObjCMethodDecl *MD);
      
      /// BuildIvarLayout - Builds ivar layout bitmap for the class
      /// implementation for the __strong or __weak case.
      ///
      llvm::Constant *BuildIvarLayout(const ObjCImplementationDecl *OI,
                                      bool ForStrongLayout);
      
      llvm::Constant *BuildIvarLayoutBitmap(std::string &BitMap);
      
      void BuildAggrIvarRecordLayout(const RecordType *RT,
                                     unsigned int BytePos, bool ForStrongLayout,
                                     bool &HasUnion);
      void BuildAggrIvarLayout(const ObjCImplementationDecl *OI,
                               const llvm::StructLayout *Layout,
                               const RecordDecl *RD,
                               ArrayRef<const FieldDecl*> RecFields,
                               unsigned int BytePos, bool ForStrongLayout,
                               bool &HasUnion);
      
      Qualifiers::ObjCLifetime getBlockCaptureLifetime(QualType QT, bool ByrefLayout);
      
      void UpdateRunSkipBlockVars(bool IsByref,
                                  Qualifiers::ObjCLifetime LifeTime,
                                  CharUnits FieldOffset,
                                  CharUnits FieldSize);
      
      void BuildRCBlockVarRecordLayout(const RecordType *RT,
                                       CharUnits BytePos, bool &HasUnion,
                                       bool ByrefLayout=false);
      
      void BuildRCRecordLayout(const llvm::StructLayout *RecLayout,
                               const RecordDecl *RD,
                               ArrayRef<const FieldDecl*> RecFields,
                               CharUnits BytePos, bool &HasUnion,
                               bool ByrefLayout);
      
      uint64_t InlineLayoutInstruction(SmallVectorImpl<unsigned char> &Layout);
      
      llvm::Constant *getBitmapBlockLayout(bool ComputeByrefLayout);
      
      
      /// GetIvarLayoutName - Returns a unique constant for the given
      /// ivar layout bitmap.
      llvm::Constant *GetIvarLayoutName(IdentifierInfo *Ident,
                                        const ObjCCommonTypesHelper &ObjCTypes);
      
      /// EmitPropertyList - Emit the given property list. The return
      /// value has type PropertyListPtrTy.
      llvm::Constant *EmitPropertyList(Twine Name,
                                       const Decl *Container,
                                       const ObjCContainerDecl *OCD,
                                       const ObjCCommonTypesHelper &ObjCTypes);
      
      /// EmitProtocolMethodTypes - Generate the array of extended method type
      /// strings. The return value has type Int8PtrPtrTy.
      llvm::Constant *EmitProtocolMethodTypes(Twine Name,
                                              ArrayRef<llvm::Constant*> MethodTypes,
                                              const ObjCCommonTypesHelper &ObjCTypes);
      
      /// PushProtocolProperties - Push protocol's property on the input stack.
      void PushProtocolProperties(
                                  llvm::SmallPtrSet<const IdentifierInfo*, 16> &PropertySet,
                                  SmallVectorImpl<llvm::Constant*> &Properties,
                                  const Decl *Container,
                                  const ObjCProtocolDecl *Proto,
                                  const ObjCCommonTypesHelper &ObjCTypes);
      
      /// GetProtocolRef - Return a reference to the internal protocol
      /// description, creating an empty one if it has not been
      /// defined. The return value has type ProtocolPtrTy.
      llvm::Constant *GetProtocolRef(const ObjCProtocolDecl *PD);
      
      // common helper function, turning names into abbreviated MD5 hashes
      llvm::Constant *HashConstantForString( StringRef sref);
      
      
      /// CreateMetadataVar - Create a global variable with internal
      /// linkage for use by the Objective-C runtime.
      ///
      /// This is a convenience wrapper which not only creates the
      /// variable, but also sets the section and alignment and adds the
      /// global to the "llvm.used" list.
      ///
      /// \param Name - The variable name.
      /// \param Init - The variable initializer; this is also used to
      /// define the type of the variable.
      /// \param Section - The section the variable should go into, or empty.
      /// \param Align - The alignment for the variable, or 0.
      /// \param AddToUsed - Whether the variable should be added to
      /// "llvm.used".
      llvm::GlobalVariable *CreateMetadataVar(Twine Name, llvm::Constant *Init,
                                              StringRef Section, unsigned Align,
                                              bool AddToUsed);
      
      CodeGen::RValue EmitMessageSend(CodeGen::CodeGenFunction &CGF,
                                      ReturnValueSlot Return,
                                      QualType ResultType,
                                      llvm::Value *Sel,
                                      llvm::Value *Arg0,
                                      QualType Arg0Ty,
                                      bool IsSuper,
                                      const CallArgList &CallArgs,
                                      const ObjCMethodDecl *OMD,
                                      const ObjCCommonTypesHelper &ObjCTypes);
      
      /// EmitImageInfo - Emit the image info marker used to encode some module
      /// level information.
      void EmitImageInfo();
      
   public:
      CGObjCCommonMulleRuntime(CodeGen::CodeGenModule &cgm) :
      CGObjCRuntime(cgm), VMContext(cgm.getLLVMContext()) { }
      
      llvm::Constant *GenerateConstantString(const StringLiteral *SL) override;
      
      llvm::Function *GenerateMethod(const ObjCMethodDecl *OMD,
                                     const ObjCContainerDecl *CD=nullptr) override;
      
      void GenerateProtocol(const ObjCProtocolDecl *PD) override;
      
      /// GetOrEmitProtocol - Get the protocol object for the given
      /// declaration, emitting it if necessary. The return value has type
      /// ProtocolPtrTy.
      virtual llvm::Constant *GetOrEmitProtocol(const ObjCProtocolDecl *PD)=0;
      
      /// GetOrEmitProtocolRef - Get a forward reference to the protocol
      /// object for the given declaration, emitting it if needed. These
      /// forward references will be filled in with empty bodies if no
      /// definition is seen. The return value has type ProtocolPtrTy.
      virtual llvm::Constant *GetOrEmitProtocolRef(const ObjCProtocolDecl *PD)=0;
      llvm::Constant *BuildGCBlockLayout(CodeGen::CodeGenModule &CGM,
                                         const CGBlockInfo &blockInfo) override;
      llvm::Constant *BuildRCBlockLayout(CodeGen::CodeGenModule &CGM,
                                         const CGBlockInfo &blockInfo) override;
      
      llvm::Constant *BuildByrefLayout(CodeGen::CodeGenModule &CGM,
                                       QualType T) override;
   };
   
   
   
   /*
    *
    *
    *
    */
   class CGObjCMulleRuntime : public CGObjCCommonMulleRuntime {
   private:
      ObjCTypesHelper ObjCTypes;
      
      /// EmitModuleInfo - Another marker encoding module level
      /// information.
      void EmitModuleInfo();
      
      /// EmitModuleSymols - Emit module symbols, the list of defined
      /// classes and categories. The result has type SymtabPtrTy.
      llvm::Constant *EmitModuleSymbols();
      
      /// FinishModule - Write out global data structures at the end of
      /// processing a translation unit.
      void FinishModule();
      
      /// EmitClassExtension - Generate the class extension structure used
      /// to store the weak ivar layout and properties. The return value
      /// has type ClassExtensionPtrTy.
      llvm::Constant *EmitClassExtension(const ObjCImplementationDecl *ID);
      
      /// EmitClassRef - Return a Value*, of type ObjCTypes.ClassPtrTy,
      /// for the given class.
      llvm::Constant *EmitClassRef(CodeGenFunction &CGF,
                                const ObjCInterfaceDecl *ID);
      
      llvm::Constant *EmitClassRefFromId(CodeGenFunction &CGF,
                                      IdentifierInfo *II);
      
      llvm::Constant *EmitNSAutoreleasePoolClassRef(CodeGenFunction &CGF) override;
      
      
      /// EmitIvarList - Emit the ivar list for the given
      /// implementation. If ForClass is true the list of class ivars
      /// (i.e. metaclass ivars) is emitted, otherwise the list of
      /// interface ivars will be emitted. The return value has type
      /// IvarListPtrTy.
      llvm::Constant *EmitIvarList(const ObjCImplementationDecl *ID,
                                   bool ForClass);
      
      llvm::Constant *GetMethodConstant(const ObjCMethodDecl *MD);
      
      llvm::Constant *GetMethodDescriptionConstant(const ObjCMethodDecl *MD);
      
      /// EmitMethodList - Emit the method list for the given
      /// implementation. The return value has type MethodListPtrTy.
      llvm::Constant *EmitMethodList(Twine Name,
                                     const char *Section,
                                     ArrayRef<llvm::Constant*> Methods);
      
      /// EmitMethodDescList - Emit a method description list for a list of
      /// method declarations.
      ///  - TypeName: The name for the type containing the methods.
      ///  - IsProtocol: True iff these methods are for a protocol.
      ///  - ClassMethds: True iff these are class methods.
      ///  - Required: When true, only "required" methods are
      ///    listed. Similarly, when false only "optional" methods are
      ///    listed. For classes this should always be true.
      ///  - begin, end: The method list to output.
      ///
      /// The return value has type MethodDescriptionListPtrTy.
      llvm::Constant *EmitMethodDescList(Twine Name,
                                         const char *Section,
                                         ArrayRef<llvm::Constant*> Methods);
      
      /// GetOrEmitProtocol - Get the protocol object for the given
      /// declaration, emitting it if necessary. The return value has type
      /// ProtocolPtrTy.
      llvm::Constant *GetOrEmitProtocol(const ObjCProtocolDecl *PD) override;
      
      /// GetOrEmitProtocolRef - Get a forward reference to the protocol
      /// object for the given declaration, emitting it if needed. These
      /// forward references will be filled in with empty bodies if no
      /// definition is seen. The return value has type ProtocolPtrTy.
      llvm::Constant *GetOrEmitProtocolRef(const ObjCProtocolDecl *PD) override;
      

      /// EmitProtocolIDList - Generate the list of referenced
      /// protocols. The return value has type ProtocolListPtrTy.
      llvm::Constant *EmitProtocolIDList(Twine Name,
                                         ObjCProtocolDecl::protocol_iterator begin,
                                         ObjCProtocolDecl::protocol_iterator end);

      
      /// EmitSelector - Return a Value*, of type SelectorIDTy,
      /// for the given selector.
      llvm::Constant *EmitSelector(CodeGenFunction &CGF, Selector Sel,
                                bool lval=false);
      
   public:
      CGObjCMulleRuntime(CodeGen::CodeGenModule &cgm);
      
      llvm::Function *ModuleInitFunction() override;
      
      CodeGen::RValue GenerateMessageSend(CodeGen::CodeGenFunction &CGF,
                                          ReturnValueSlot Return,
                                          QualType ResultType,
                                          Selector Sel, llvm::Value *Receiver,
                                          const CallArgList &CallArgs,
                                          const ObjCInterfaceDecl *Class,
                                          const ObjCMethodDecl *Method) override;
      
      CodeGen::RValue
      GenerateMessageSendSuper(CodeGen::CodeGenFunction &CGF,
                               ReturnValueSlot Return, QualType ResultType,
                               Selector Sel, const ObjCInterfaceDecl *Class,
                               bool isCategoryImpl, llvm::Value *Receiver,
                               bool IsClassMessage, const CallArgList &CallArgs,
                               const ObjCMethodDecl *Method) override;
      
      llvm::Value *GetClass(CodeGenFunction &CGF,
                            const ObjCInterfaceDecl *ID) override;
      
      llvm::Constant *GetSelector(CodeGenFunction &CGF, Selector Sel,
                               bool lval = false) override;
      
      /// The NeXT/Apple runtimes do not support typed selectors; just emit an
      /// untyped one.
      llvm::Constant *GetSelector(CodeGenFunction &CGF,
                               const ObjCMethodDecl *Method) override;
      
      llvm::Constant *GetEHType(QualType T) override;
      
      void GenerateCategory(const ObjCCategoryImplDecl *CMD) override;
      
      void GenerateClass(const ObjCImplementationDecl *ClassDecl) override;
      
      void RegisterAlias(const ObjCCompatibleAliasDecl *OAD) override {}
      
      llvm::Value *GenerateProtocolRef(CodeGenFunction &CGF,
                                       const ObjCProtocolDecl *PD) override;
      
      llvm::Constant *GetPropertyGetFunction() override;
      llvm::Constant *GetPropertySetFunction() override;
      llvm::Constant *GetOptimizedPropertySetFunction(bool atomic,
                                                      bool copy) override;
      llvm::Constant *GetGetStructFunction() override;
      llvm::Constant *GetSetStructFunction() override;
      llvm::Constant *GetCppAtomicObjectGetFunction() override;
      llvm::Constant *GetCppAtomicObjectSetFunction() override;
      llvm::Constant *EnumerationMutationFunction() override;
      
      void EmitTryStmt(CodeGen::CodeGenFunction &CGF,
                       const ObjCAtTryStmt &S) override;
      void EmitSynchronizedStmt(CodeGen::CodeGenFunction &CGF,
                                const ObjCAtSynchronizedStmt &S) override;
      void EmitTryOrSynchronizedStmt(CodeGen::CodeGenFunction &CGF, const Stmt &S);
      void EmitThrowStmt(CodeGen::CodeGenFunction &CGF, const ObjCAtThrowStmt &S,
                         bool ClearInsertionPoint=true) override;
      llvm::Value * EmitObjCWeakRead(CodeGen::CodeGenFunction &CGF,
                                     llvm::Value *AddrWeakObj) override;
      void EmitObjCWeakAssign(CodeGen::CodeGenFunction &CGF,
                              llvm::Value *src, llvm::Value *dst) override;
      void EmitObjCGlobalAssign(CodeGen::CodeGenFunction &CGF,
                                llvm::Value *src, llvm::Value *dest,
                                bool threadlocal = false) override;
      void EmitObjCIvarAssign(CodeGen::CodeGenFunction &CGF,
                              llvm::Value *src, llvm::Value *dest,
                              llvm::Value *ivarOffset) override;
      void EmitObjCStrongCastAssign(CodeGen::CodeGenFunction &CGF,
                                    llvm::Value *src, llvm::Value *dest) override;
      void EmitGCMemmoveCollectable(CodeGen::CodeGenFunction &CGF,
                                    llvm::Value *dest, llvm::Value *src,
                                    llvm::Value *size) override;
      
      LValue EmitObjCValueForIvar(CodeGen::CodeGenFunction &CGF, QualType ObjectTy,
                                  llvm::Value *BaseValue, const ObjCIvarDecl *Ivar,
                                  unsigned CVRQualifiers) override;
      llvm::Value *EmitIvarOffset(CodeGen::CodeGenFunction &CGF,
                                  const ObjCInterfaceDecl *Interface,
                                  const ObjCIvarDecl *Ivar) override;
      
      /// GetClassGlobal - Return the global variable for the Objective-C
      /// class of the given name.
      llvm::GlobalVariable *GetClassGlobal(const std::string &Name,
                                           bool Weak = false) override {
         llvm_unreachable("CGObjCMulleRuntime::GetClassGlobal");
      }
   };
}

/// A helper class for performing the null-initialization of a return
/// value.
struct NullReturnState {
   llvm::BasicBlock *NullBB;
   NullReturnState() : NullBB(nullptr) {}
   
   /// Perform a null-check of the given receiver.
   void init(CodeGenFunction &CGF, llvm::Value *receiver) {
      // Make blocks for the null-receiver and call edges.
      NullBB = CGF.createBasicBlock("msgSend.null-receiver");
      llvm::BasicBlock *callBB = CGF.createBasicBlock("msgSend.call");
      
      // Check for a null receiver and, if there is one, jump to the
      // null-receiver block.  There's no point in trying to avoid it:
      // we're always going to put *something* there, because otherwise
      // we shouldn't have done this null-check in the first place.
      llvm::Value *isNull = CGF.Builder.CreateIsNull(receiver);
      CGF.Builder.CreateCondBr(isNull, NullBB, callBB);
      
      // Otherwise, start performing the call.
      CGF.EmitBlock(callBB);
   }
   
   /// Complete the null-return operation.  It is valid to call this
   /// regardless of whether 'init' has been called.
   RValue complete(CodeGenFunction &CGF, RValue result, QualType resultType,
                   const CallArgList &CallArgs,
                   const ObjCMethodDecl *Method) {
      // If we never had to do a null-check, just use the raw result.
      if (!NullBB) return result;
      
      // The continuation block.  This will be left null if we don't have an
      // IP, which can happen if the method we're calling is marked noreturn.
      llvm::BasicBlock *contBB = nullptr;
      
      // Finish the call path.
      llvm::BasicBlock *callBB = CGF.Builder.GetInsertBlock();
      if (callBB) {
         contBB = CGF.createBasicBlock("msgSend.cont");
         CGF.Builder.CreateBr(contBB);
      }
      
      // Okay, start emitting the null-receiver block.
      CGF.EmitBlock(NullBB);
      
      // Release any consumed arguments we've got.
      if (Method) {
         CallArgList::const_iterator I = CallArgs.begin();
         for (ObjCMethodDecl::param_const_iterator i = Method->param_begin(),
              e = Method->param_end(); i != e; ++i, ++I) {
            const ParmVarDecl *ParamDecl = (*i);
            if (ParamDecl->hasAttr<NSConsumedAttr>()) {
               RValue RV = I->RV;
               assert(RV.isScalar() &&
                      "NullReturnState::complete - arg not on object");
               CGF.EmitARCRelease(RV.getScalarVal(), ARCImpreciseLifetime);
            }
         }
      }
      
      // The phi code below assumes that we haven't needed any control flow yet.
      assert(CGF.Builder.GetInsertBlock() == NullBB);
      
      // If we've got a void return, just jump to the continuation block.
      if (result.isScalar() && resultType->isVoidType()) {
         // No jumps required if the message-send was noreturn.
         if (contBB) CGF.EmitBlock(contBB);
         return result;
      }
      
      // If we've got a scalar return, build a phi.
      if (result.isScalar()) {
         // Derive the null-initialization value.
         llvm::Constant *null = CGF.CGM.EmitNullConstant(resultType);
         
         // If no join is necessary, just flow out.
         if (!contBB) return RValue::get(null);
         
         // Otherwise, build a phi.
         CGF.EmitBlock(contBB);
         llvm::PHINode *phi = CGF.Builder.CreatePHI(null->getType(), 2);
         phi->addIncoming(result.getScalarVal(), callBB);
         phi->addIncoming(null, NullBB);
         return RValue::get(phi);
      }
      
      // If we've got an aggregate return, null the buffer out.
      // FIXME: maybe we should be doing things differently for all the
      // cases where the ABI has us returning (1) non-agg values in
      // memory or (2) agg values in registers.
      if (result.isAggregate()) {
         assert(result.isAggregate() && "null init of non-aggregate result?");
         CGF.EmitNullInitialization(result.getAggregateAddr(), resultType);
         if (contBB) CGF.EmitBlock(contBB);
         return result;
      }
      
      // Complex types.
      CGF.EmitBlock(contBB);
      CodeGenFunction::ComplexPairTy callResult = result.getComplexVal();
      
      // Find the scalar type and its zero value.
      llvm::Type *scalarTy = callResult.first->getType();
      llvm::Constant *scalarZero = llvm::Constant::getNullValue(scalarTy);
      
      // Build phis for both coordinates.
      llvm::PHINode *real = CGF.Builder.CreatePHI(scalarTy, 2);
      real->addIncoming(callResult.first, callBB);
      real->addIncoming(scalarZero, NullBB);
      llvm::PHINode *imag = CGF.Builder.CreatePHI(scalarTy, 2);
      imag->addIncoming(callResult.second, callBB);
      imag->addIncoming(scalarZero, NullBB);
      return RValue::getComplex(real, imag);
   }
};


/* *** Helper Functions *** */

/// getConstantGEP() - Help routine to construct simple GEPs.
static llvm::Constant *getConstantGEP(llvm::LLVMContext &VMContext,
                                      llvm::Constant *C,
                                      unsigned idx0,
                                      unsigned idx1) {
   llvm::Value *Idxs[] = {
      llvm::ConstantInt::get(llvm::Type::getInt32Ty(VMContext), idx0),
      llvm::ConstantInt::get(llvm::Type::getInt32Ty(VMContext), idx1)
   };
   return llvm::ConstantExpr::getGetElementPtr(C, Idxs);
}

/// hasObjCExceptionAttribute - Return true if this class or any super
/// class has the __objc_exception__ attribute.
static bool hasObjCExceptionAttribute(ASTContext &Context,
                                      const ObjCInterfaceDecl *OID) {
   if (OID->hasAttr<ObjCExceptionAttr>())
      return true;
   if (const ObjCInterfaceDecl *Super = OID->getSuperClass())
      return hasObjCExceptionAttribute(Context, Super);
   return false;
}

/* *** CGObjCMulleRuntime Public Interface *** */

CGObjCMulleRuntime::CGObjCMulleRuntime(CodeGen::CodeGenModule &cgm) : CGObjCCommonMulleRuntime(cgm),
ObjCTypes(cgm) {
   ObjCABI = 1;
   EmitImageInfo();
}

/// GetClass - Return a reference to the class for the given interface
/// decl.
llvm::Value *CGObjCMulleRuntime::GetClass(CodeGenFunction &CGF,
                                 const ObjCInterfaceDecl *ID)
{
   // call mulle_objc_runtime_get_class()
   llvm::Value  *classID;
   llvm::Value *rval;
   llvm::Value *classPtr;
   
   classID  = HashConstantForString( ID->getName());
   classPtr = CGF.EmitNounwindRuntimeCall(ObjCTypes.getGetRuntimeClassFn(),
                               classID, "mulle_objc_unfailing_get_class"); // string what for ??
   rval     = CGF.Builder.CreateBitCast( classPtr, ObjCTypes.ObjectPtrTy);
   return rval;
}

/// GetSelector - Return the pointer to the unique'd string for this selector.
llvm::Constant *CGObjCMulleRuntime::GetSelector(CodeGenFunction &CGF, Selector Sel,
                                    bool lval) {
   return EmitSelector(CGF, Sel, lval);
}
llvm::Constant *CGObjCMulleRuntime::GetSelector(CodeGenFunction &CGF, const ObjCMethodDecl
                                    *Method) {
   return EmitSelector(CGF, Method->getSelector());
}

llvm::Constant *CGObjCMulleRuntime::GetEHType(QualType T) {
   if (T->isObjCIdType() ||
       T->isObjCQualifiedIdType()) {
      return CGM.GetAddrOfRTTIDescriptor(
                                         CGM.getContext().getObjCIdRedefinitionType(), /*ForEH=*/true);
   }
   if (T->isObjCClassType() ||
       T->isObjCQualifiedClassType()) {
      return CGM.GetAddrOfRTTIDescriptor(
                                         CGM.getContext().getObjCClassRedefinitionType(), /*ForEH=*/true);
   }
   if (T->isObjCObjectPointerType())
      return CGM.GetAddrOfRTTIDescriptor(T,  /*ForEH=*/true);
   
   llvm_unreachable("asking for catch type for ObjC type in fragile runtime");
}

/// Generate a constant CFString object.
/*
 struct __builtin_CFString {
 const int *isa; // point to __CFConstantStringClassReference
 int flags;
 const char *str;
 long length;
 };
 */

/// or Generate a constant NSString object.
/*
 struct __builtin_NSString {
 const int *isa; // point to __NSConstantStringClassReference
 const char *str;
 unsigned int length;
 };
 */

//
// { LONG_MAX, MULLE_OBJC_METHOD_ID( 0x423aeba60e4eb3be), "VfL Bochum 1848" };
// 0x423aeba60e4eb3be == NXConstantString
//

llvm::Constant *CGObjCCommonMulleRuntime::GenerateConstantString(
                                                        const StringLiteral *SL) {
   return (CGM.getLangOpts().NoConstantCFStrings == 0 ?
           CGM.GetAddrOfConstantCFString(SL) :
           CGM.GetAddrOfConstantString(SL));
}

enum {
   kCFTaggedObjectID_Integer = (1 << 1) + 1
};

/// Generates a message send where the super is the receiver.  This is
/// a message send to self with special delivery semantics indicating
/// which class's method should be called.
CodeGen::RValue
CGObjCMulleRuntime::GenerateMessageSendSuper(CodeGen::CodeGenFunction &CGF,
                                    ReturnValueSlot Return,
                                    QualType ResultType,
                                    Selector Sel,
                                    const ObjCInterfaceDecl *Class,
                                    bool isCategoryImpl,
                                    llvm::Value *Receiver,
                                    bool IsClassMessage,
                                    const CodeGen::CallArgList &CallArgs,
                                    const ObjCMethodDecl *Method)
{
   // If this is a class message the metaclass is passed as the target.
   llvm::Value *Target;

   if (IsClassMessage)
      Target = EmitClassRef(CGF, Class->getSuperClass());  // FIXME: infraClass
   else
      Target = EmitClassRef(CGF, Class->getSuperClass());
   
   return EmitMessageSend(CGF, Return, ResultType,
                               EmitSelector(CGF, Sel),
                          Target, ObjCTypes.SuperPtrCTy,
                          true, CallArgs, Method, ObjCTypes);
}

/// Generate code for a message send expression.
CodeGen::RValue CGObjCMulleRuntime::GenerateMessageSend(CodeGen::CodeGenFunction &CGF,
                                               ReturnValueSlot Return,
                                               QualType ResultType,
                                               Selector Sel,
                                               llvm::Value *Receiver,
                                               const CallArgList &CallArgs,
                                               const ObjCInterfaceDecl *Class,
                                               const ObjCMethodDecl *Method)
{
   llvm::Value *Arg0;
   QualType Arg0Ty;
   CallArgList ActualArgs;
   llvm::Value   *selID;
   
   selID = EmitSelector(CGF, Sel);
   
   Arg0 = CGF.Builder.CreateBitCast(Receiver, ObjCTypes.ObjectPtrTy);
   ActualArgs.add(RValue::get(Arg0), CGF.getContext().getObjCInstanceType());
   ActualArgs.add(RValue::get(selID), CGF.getContext().getObjCSelType());
   ActualArgs.addFrom(CallArgs);
   
   // If we're calling a method, use the formal signature.
   MessageSendInfo MSI = getMessageSendInfo( Method, ResultType, ActualArgs);
   
   if (Method)
      assert(CGM.getContext().getCanonicalType(Method->getReturnType()) ==
             CGM.getContext().getCanonicalType(ResultType) &&
             "Result type mismatch!");
   
   NullReturnState nullReturn;
   
   llvm::Constant *Fn = nullptr;
   if (CGM.ReturnSlotInterferesWithArgs(MSI.CallInfo))
   {
         nullReturn.init(CGF, Arg0);
   }
   
   Fn = ObjCTypes.getMessageSendFn();
   
   bool requiresnullCheck = false;
   
   if (CGM.getLangOpts().ObjCAutoRefCount && Method)
      for (const auto *ParamDecl : Method->params())
      {
         if (ParamDecl->hasAttr<NSConsumedAttr>())
         {
            if (!nullReturn.NullBB)
               nullReturn.init(CGF, Arg0);
            requiresnullCheck = true;
            break;
         }
      }
   
   Fn = llvm::ConstantExpr::getBitCast(Fn, MSI.MessengerType);
   RValue rvalue = CGF.EmitCall(MSI.CallInfo, Fn, Return, ActualArgs);
   
   return nullReturn.complete(CGF, rvalue, ResultType, CallArgs,
                              requiresnullCheck ? Method : nullptr);
}

CodeGen::RValue
CGObjCCommonMulleRuntime::EmitMessageSend(CodeGen::CodeGenFunction &CGF,
                                 ReturnValueSlot Return,
                                 QualType ResultType,
                                 llvm::Value *Sel,
                                 llvm::Value *Arg0,
                                 QualType Arg0Ty,
                                 bool IsSuper,
                                 const CallArgList &CallArgs,
                                 const ObjCMethodDecl *Method,
                                 const ObjCCommonTypesHelper &ObjCTypes) {
   CallArgList ActualArgs;
   
   Arg0 = CGF.Builder.CreateBitCast(Arg0, ObjCTypes.ObjectPtrTy);
   ActualArgs.add(RValue::get(Arg0), Arg0Ty);
   ActualArgs.add(RValue::get(Sel), CGF.getContext().getObjCSelType());
   ActualArgs.addFrom(CallArgs);
   
   // If we're calling a method, use the formal signature.
   MessageSendInfo MSI = getMessageSendInfo(Method, ResultType, ActualArgs);
   
   if (Method)
      assert(CGM.getContext().getCanonicalType(Method->getReturnType()) ==
             CGM.getContext().getCanonicalType(ResultType) &&
             "Result type mismatch!");
   
   NullReturnState nullReturn;
   
   llvm::Constant *Fn = nullptr;
   if (CGM.ReturnSlotInterferesWithArgs(MSI.CallInfo))
   {
      if (!IsSuper)
         nullReturn.init(CGF, Arg0);
   }

   if( IsSuper)
      Fn = ObjCTypes.getMessageSendSuperFn();
   else
      Fn = ObjCTypes.getMessageSendFn();

   bool requiresnullCheck = false;
   
   if (CGM.getLangOpts().ObjCAutoRefCount && Method)
      for (const auto *ParamDecl : Method->params())
      {
         if (ParamDecl->hasAttr<NSConsumedAttr>())
         {
            if (!nullReturn.NullBB)
               nullReturn.init(CGF, Arg0);
            requiresnullCheck = true;
            break;
         }
      }
   
   Fn = llvm::ConstantExpr::getBitCast(Fn, MSI.MessengerType);
   RValue rvalue = CGF.EmitCall(MSI.CallInfo, Fn, Return, ActualArgs);
   return nullReturn.complete(CGF, rvalue, ResultType, CallArgs,
                              requiresnullCheck ? Method : nullptr);
}

#pragma mark -
#pragma mark blocks

static Qualifiers::GC GetGCAttrTypeForType(ASTContext &Ctx, QualType FQT) {
   if (FQT.isObjCGCStrong())
      return Qualifiers::Strong;
   
   if (FQT.isObjCGCWeak() || FQT.getObjCLifetime() == Qualifiers::OCL_Weak)
      return Qualifiers::Weak;
   
   // check for __unsafe_unretained
   if (FQT.getObjCLifetime() == Qualifiers::OCL_ExplicitNone)
      return Qualifiers::GCNone;
   
   if (FQT->isObjCObjectPointerType() || FQT->isBlockPointerType())
      return Qualifiers::Strong;
   
   if (const PointerType *PT = FQT->getAs<PointerType>())
      return GetGCAttrTypeForType(Ctx, PT->getPointeeType());
   
   return Qualifiers::GCNone;
}

llvm::Constant *CGObjCCommonMulleRuntime::BuildGCBlockLayout(CodeGenModule &CGM,
                                                    const CGBlockInfo &blockInfo) {
   
   llvm::Constant *nullPtr = llvm::Constant::getNullValue(CGM.Int8PtrTy);
   if (CGM.getLangOpts().getGC() == LangOptions::NonGC &&
       !CGM.getLangOpts().ObjCAutoRefCount)
      return nullPtr;
   
   bool hasUnion = false;
   SkipIvars.clear();
   IvarsInfo.clear();
   unsigned WordSizeInBits = CGM.getTarget().getPointerWidth(0);
   unsigned ByteSizeInBits = CGM.getTarget().getCharWidth();
   
   // __isa is the first field in block descriptor and must assume by runtime's
   // convention that it is GC'able.
   IvarsInfo.push_back(GC_IVAR(0, 1));
   
   const BlockDecl *blockDecl = blockInfo.getBlockDecl();
   
   // Calculate the basic layout of the block structure.
   const llvm::StructLayout *layout =
   CGM.getDataLayout().getStructLayout(blockInfo.StructureType);
   
   // Ignore the optional 'this' capture: C++ objects are not assumed
   // to be GC'ed.
   
   // Walk the captured variables.
   for (const auto &CI : blockDecl->captures()) {
      const VarDecl *variable = CI.getVariable();
      QualType type = variable->getType();
      
      const CGBlockInfo::Capture &capture = blockInfo.getCapture(variable);
      
      // Ignore constant captures.
      if (capture.isConstant()) continue;
      
      uint64_t fieldOffset = layout->getElementOffset(capture.getIndex());
      
      // __block variables are passed by their descriptor address.
      if (CI.isByRef()) {
         IvarsInfo.push_back(GC_IVAR(fieldOffset, /*size in words*/ 1));
         continue;
      }
      
      assert(!type->isArrayType() && "array variable should not be caught");
      if (const RecordType *record = type->getAs<RecordType>()) {
         BuildAggrIvarRecordLayout(record, fieldOffset, true, hasUnion);
         continue;
      }
      
      Qualifiers::GC GCAttr = GetGCAttrTypeForType(CGM.getContext(), type);
      unsigned fieldSize = CGM.getContext().getTypeSize(type);
      
      if (GCAttr == Qualifiers::Strong)
         IvarsInfo.push_back(GC_IVAR(fieldOffset,
                                     fieldSize / WordSizeInBits));
      else if (GCAttr == Qualifiers::GCNone || GCAttr == Qualifiers::Weak)
         SkipIvars.push_back(GC_IVAR(fieldOffset,
                                     fieldSize / ByteSizeInBits));
   }
   
   if (IvarsInfo.empty())
      return nullPtr;
   
   // Sort on byte position; captures might not be allocated in order,
   // and unions can do funny things.
   llvm::array_pod_sort(IvarsInfo.begin(), IvarsInfo.end());
   llvm::array_pod_sort(SkipIvars.begin(), SkipIvars.end());
   
   std::string BitMap;
   llvm::Constant *C = BuildIvarLayoutBitmap(BitMap);
   if (CGM.getLangOpts().ObjCGCBitmapPrint) {
      printf("\n block variable layout for block: ");
      const unsigned char *s = (const unsigned char*)BitMap.c_str();
      for (unsigned i = 0, e = BitMap.size(); i < e; i++)
         if (!(s[i] & 0xf0))
            printf("0x0%x%s", s[i], s[i] != 0 ? ", " : "");
         else
            printf("0x%x%s",  s[i], s[i] != 0 ? ", " : "");
      printf("\n");
   }
   
   return C;
}

/// getBlockCaptureLifetime - This routine returns life time of the captured
/// block variable for the purpose of block layout meta-data generation. FQT is
/// the type of the variable captured in the block.
Qualifiers::ObjCLifetime CGObjCCommonMulleRuntime::getBlockCaptureLifetime(QualType FQT,
                                                                  bool ByrefLayout) {
   if (CGM.getLangOpts().ObjCAutoRefCount)
      return FQT.getObjCLifetime();
   
   // MRR.
   if (FQT->isObjCObjectPointerType() || FQT->isBlockPointerType())
      return ByrefLayout ? Qualifiers::OCL_ExplicitNone : Qualifiers::OCL_Strong;
   
   return Qualifiers::OCL_None;
}

void CGObjCCommonMulleRuntime::UpdateRunSkipBlockVars(bool IsByref,
                                             Qualifiers::ObjCLifetime LifeTime,
                                             CharUnits FieldOffset,
                                             CharUnits FieldSize) {
   // __block variables are passed by their descriptor address.
   if (IsByref)
      RunSkipBlockVars.push_back(RUN_SKIP(BLOCK_LAYOUT_BYREF, FieldOffset,
                                          FieldSize));
   else if (LifeTime == Qualifiers::OCL_Strong)
      RunSkipBlockVars.push_back(RUN_SKIP(BLOCK_LAYOUT_STRONG, FieldOffset,
                                          FieldSize));
   else if (LifeTime == Qualifiers::OCL_Weak)
      RunSkipBlockVars.push_back(RUN_SKIP(BLOCK_LAYOUT_WEAK, FieldOffset,
                                          FieldSize));
   else if (LifeTime == Qualifiers::OCL_ExplicitNone)
      RunSkipBlockVars.push_back(RUN_SKIP(BLOCK_LAYOUT_UNRETAINED, FieldOffset,
                                          FieldSize));
   else
      RunSkipBlockVars.push_back(RUN_SKIP(BLOCK_LAYOUT_NON_OBJECT_BYTES,
                                          FieldOffset,
                                          FieldSize));
}

void CGObjCCommonMulleRuntime::BuildRCRecordLayout(const llvm::StructLayout *RecLayout,
                                          const RecordDecl *RD,
                                          ArrayRef<const FieldDecl*> RecFields,
                                          CharUnits BytePos, bool &HasUnion,
                                          bool ByrefLayout) {
   bool IsUnion = (RD && RD->isUnion());
   CharUnits MaxUnionSize = CharUnits::Zero();
   const FieldDecl *MaxField = nullptr;
   const FieldDecl *LastFieldBitfieldOrUnnamed = nullptr;
   CharUnits MaxFieldOffset = CharUnits::Zero();
   CharUnits LastBitfieldOrUnnamedOffset = CharUnits::Zero();
   
   if (RecFields.empty())
      return;
   unsigned ByteSizeInBits = CGM.getTarget().getCharWidth();
   
   for (unsigned i = 0, e = RecFields.size(); i != e; ++i) {
      const FieldDecl *Field = RecFields[i];
      // Note that 'i' here is actually the field index inside RD of Field,
      // although this dependency is hidden.
      const ASTRecordLayout &RL = CGM.getContext().getASTRecordLayout(RD);
      CharUnits FieldOffset =
      CGM.getContext().toCharUnitsFromBits(RL.getFieldOffset(i));
      
      // Skip over unnamed or bitfields
      if (!Field->getIdentifier() || Field->isBitField()) {
         LastFieldBitfieldOrUnnamed = Field;
         LastBitfieldOrUnnamedOffset = FieldOffset;
         continue;
      }
      
      LastFieldBitfieldOrUnnamed = nullptr;
      QualType FQT = Field->getType();
      if (FQT->isRecordType() || FQT->isUnionType()) {
         if (FQT->isUnionType())
            HasUnion = true;
         
         BuildRCBlockVarRecordLayout(FQT->getAs<RecordType>(),
                                     BytePos + FieldOffset, HasUnion);
         continue;
      }
      
      if (const ArrayType *Array = CGM.getContext().getAsArrayType(FQT)) {
         const ConstantArrayType *CArray =
         dyn_cast_or_null<ConstantArrayType>(Array);
         uint64_t ElCount = CArray->getSize().getZExtValue();
         assert(CArray && "only array with known element size is supported");
         FQT = CArray->getElementType();
         while (const ArrayType *Array = CGM.getContext().getAsArrayType(FQT)) {
            const ConstantArrayType *CArray =
            dyn_cast_or_null<ConstantArrayType>(Array);
            ElCount *= CArray->getSize().getZExtValue();
            FQT = CArray->getElementType();
         }
         if (FQT->isRecordType() && ElCount) {
            int OldIndex = RunSkipBlockVars.size() - 1;
            const RecordType *RT = FQT->getAs<RecordType>();
            BuildRCBlockVarRecordLayout(RT, BytePos + FieldOffset,
                                        HasUnion);
            
            // Replicate layout information for each array element. Note that
            // one element is already done.
            uint64_t ElIx = 1;
            for (int FirstIndex = RunSkipBlockVars.size() - 1 ;ElIx < ElCount; ElIx++) {
               CharUnits Size = CGM.getContext().getTypeSizeInChars(RT);
               for (int i = OldIndex+1; i <= FirstIndex; ++i)
                  RunSkipBlockVars.push_back(
                                             RUN_SKIP(RunSkipBlockVars[i].opcode,
                                                      RunSkipBlockVars[i].block_var_bytepos + Size*ElIx,
                                                      RunSkipBlockVars[i].block_var_size));
            }
            continue;
         }
      }
      CharUnits FieldSize = CGM.getContext().getTypeSizeInChars(Field->getType());
      if (IsUnion) {
         CharUnits UnionIvarSize = FieldSize;
         if (UnionIvarSize > MaxUnionSize) {
            MaxUnionSize = UnionIvarSize;
            MaxField = Field;
            MaxFieldOffset = FieldOffset;
         }
      } else {
         UpdateRunSkipBlockVars(false,
                                getBlockCaptureLifetime(FQT, ByrefLayout),
                                BytePos + FieldOffset,
                                FieldSize);
      }
   }
   
   if (LastFieldBitfieldOrUnnamed) {
      if (LastFieldBitfieldOrUnnamed->isBitField()) {
         // Last field was a bitfield. Must update the info.
         uint64_t BitFieldSize
         = LastFieldBitfieldOrUnnamed->getBitWidthValue(CGM.getContext());
         unsigned UnsSize = (BitFieldSize / ByteSizeInBits) +
         ((BitFieldSize % ByteSizeInBits) != 0);
         CharUnits Size = CharUnits::fromQuantity(UnsSize);
         Size += LastBitfieldOrUnnamedOffset;
         UpdateRunSkipBlockVars(false,
                                getBlockCaptureLifetime(LastFieldBitfieldOrUnnamed->getType(),
                                                        ByrefLayout),
                                BytePos + LastBitfieldOrUnnamedOffset,
                                Size);
      } else {
         assert(!LastFieldBitfieldOrUnnamed->getIdentifier() &&"Expected unnamed");
         // Last field was unnamed. Must update skip info.
         CharUnits FieldSize
         = CGM.getContext().getTypeSizeInChars(LastFieldBitfieldOrUnnamed->getType());
         UpdateRunSkipBlockVars(false,
                                getBlockCaptureLifetime(LastFieldBitfieldOrUnnamed->getType(),
                                                        ByrefLayout),
                                BytePos + LastBitfieldOrUnnamedOffset,
                                FieldSize);
      }
   }
   
   if (MaxField)
      UpdateRunSkipBlockVars(false,
                             getBlockCaptureLifetime(MaxField->getType(), ByrefLayout),
                             BytePos + MaxFieldOffset,
                             MaxUnionSize);
}

void CGObjCCommonMulleRuntime::BuildRCBlockVarRecordLayout(const RecordType *RT,
                                                  CharUnits BytePos,
                                                  bool &HasUnion,
                                                  bool ByrefLayout) {
   const RecordDecl *RD = RT->getDecl();
   SmallVector<const FieldDecl*, 16> Fields(RD->fields());
   llvm::Type *Ty = CGM.getTypes().ConvertType(QualType(RT, 0));
   const llvm::StructLayout *RecLayout =
   CGM.getDataLayout().getStructLayout(cast<llvm::StructType>(Ty));
   
   BuildRCRecordLayout(RecLayout, RD, Fields, BytePos, HasUnion, ByrefLayout);
}

/// InlineLayoutInstruction - This routine produce an inline instruction for the
/// block variable layout if it can. If not, it returns 0. Rules are as follow:
/// If ((uintptr_t) layout) < (1 << 12), the layout is inline. In the 64bit world,
/// an inline layout of value 0x0000000000000xyz is interpreted as follows:
/// x captured object pointers of BLOCK_LAYOUT_STRONG. Followed by
/// y captured object of BLOCK_LAYOUT_BYREF. Followed by
/// z captured object of BLOCK_LAYOUT_WEAK. If any of the above is missing, zero
/// replaces it. For example, 0x00000x00 means x BLOCK_LAYOUT_STRONG and no
/// BLOCK_LAYOUT_BYREF and no BLOCK_LAYOUT_WEAK objects are captured.
uint64_t CGObjCCommonMulleRuntime::InlineLayoutInstruction(
                                                  SmallVectorImpl<unsigned char> &Layout) {
   uint64_t Result = 0;
   if (Layout.size() <= 3) {
      unsigned size = Layout.size();
      unsigned strong_word_count = 0, byref_word_count=0, weak_word_count=0;
      unsigned char inst;
      enum BLOCK_LAYOUT_OPCODE opcode ;
      switch (size) {
         case 3:
            inst = Layout[0];
            opcode = (enum BLOCK_LAYOUT_OPCODE) (inst >> 4);
            if (opcode == BLOCK_LAYOUT_STRONG)
               strong_word_count = (inst & 0xF)+1;
            else
               return 0;
            inst = Layout[1];
            opcode = (enum BLOCK_LAYOUT_OPCODE) (inst >> 4);
            if (opcode == BLOCK_LAYOUT_BYREF)
               byref_word_count = (inst & 0xF)+1;
            else
               return 0;
            inst = Layout[2];
            opcode = (enum BLOCK_LAYOUT_OPCODE) (inst >> 4);
            if (opcode == BLOCK_LAYOUT_WEAK)
               weak_word_count = (inst & 0xF)+1;
            else
               return 0;
            break;
            
         case 2:
            inst = Layout[0];
            opcode = (enum BLOCK_LAYOUT_OPCODE) (inst >> 4);
            if (opcode == BLOCK_LAYOUT_STRONG) {
               strong_word_count = (inst & 0xF)+1;
               inst = Layout[1];
               opcode = (enum BLOCK_LAYOUT_OPCODE) (inst >> 4);
               if (opcode == BLOCK_LAYOUT_BYREF)
                  byref_word_count = (inst & 0xF)+1;
               else if (opcode == BLOCK_LAYOUT_WEAK)
                  weak_word_count = (inst & 0xF)+1;
               else
                  return 0;
            }
            else if (opcode == BLOCK_LAYOUT_BYREF) {
               byref_word_count = (inst & 0xF)+1;
               inst = Layout[1];
               opcode = (enum BLOCK_LAYOUT_OPCODE) (inst >> 4);
               if (opcode == BLOCK_LAYOUT_WEAK)
                  weak_word_count = (inst & 0xF)+1;
               else
                  return 0;
            }
            else
               return 0;
            break;
            
         case 1:
            inst = Layout[0];
            opcode = (enum BLOCK_LAYOUT_OPCODE) (inst >> 4);
            if (opcode == BLOCK_LAYOUT_STRONG)
               strong_word_count = (inst & 0xF)+1;
            else if (opcode == BLOCK_LAYOUT_BYREF)
               byref_word_count = (inst & 0xF)+1;
            else if (opcode == BLOCK_LAYOUT_WEAK)
               weak_word_count = (inst & 0xF)+1;
            else
               return 0;
            break;
            
         default:
            return 0;
      }
      
      // Cannot inline when any of the word counts is 15. Because this is one less
      // than the actual work count (so 15 means 16 actual word counts),
      // and we can only display 0 thru 15 word counts.
      if (strong_word_count == 16 || byref_word_count == 16 || weak_word_count == 16)
         return 0;
      
      unsigned count =
      (strong_word_count != 0) + (byref_word_count != 0) + (weak_word_count != 0);
      
      if (size == count) {
         if (strong_word_count)
            Result = strong_word_count;
         Result <<= 4;
         if (byref_word_count)
            Result += byref_word_count;
         Result <<= 4;
         if (weak_word_count)
            Result += weak_word_count;
      }
   }
   return Result;
}

llvm::Constant *CGObjCCommonMulleRuntime::getBitmapBlockLayout(bool ComputeByrefLayout) {
   llvm::Constant *nullPtr = llvm::Constant::getNullValue(CGM.Int8PtrTy);
   if (RunSkipBlockVars.empty())
      return nullPtr;
   unsigned WordSizeInBits = CGM.getTarget().getPointerWidth(0);
   unsigned ByteSizeInBits = CGM.getTarget().getCharWidth();
   unsigned WordSizeInBytes = WordSizeInBits/ByteSizeInBits;
   
   // Sort on byte position; captures might not be allocated in order,
   // and unions can do funny things.
   llvm::array_pod_sort(RunSkipBlockVars.begin(), RunSkipBlockVars.end());
   SmallVector<unsigned char, 16> Layout;
   
   unsigned size = RunSkipBlockVars.size();
   for (unsigned i = 0; i < size; i++) {
      enum BLOCK_LAYOUT_OPCODE opcode = RunSkipBlockVars[i].opcode;
      CharUnits start_byte_pos = RunSkipBlockVars[i].block_var_bytepos;
      CharUnits end_byte_pos = start_byte_pos;
      unsigned j = i+1;
      while (j < size) {
         if (opcode == RunSkipBlockVars[j].opcode) {
            end_byte_pos = RunSkipBlockVars[j++].block_var_bytepos;
            i++;
         }
         else
            break;
      }
      CharUnits size_in_bytes =
      end_byte_pos - start_byte_pos + RunSkipBlockVars[j-1].block_var_size;
      if (j < size) {
         CharUnits gap =
         RunSkipBlockVars[j].block_var_bytepos -
         RunSkipBlockVars[j-1].block_var_bytepos - RunSkipBlockVars[j-1].block_var_size;
         size_in_bytes += gap;
      }
      CharUnits residue_in_bytes = CharUnits::Zero();
      if (opcode == BLOCK_LAYOUT_NON_OBJECT_BYTES) {
         residue_in_bytes = size_in_bytes % WordSizeInBytes;
         size_in_bytes -= residue_in_bytes;
         opcode = BLOCK_LAYOUT_NON_OBJECT_WORDS;
      }
      
      unsigned size_in_words = size_in_bytes.getQuantity() / WordSizeInBytes;
      while (size_in_words >= 16) {
         // Note that value in imm. is one less that the actual
         // value. So, 0xf means 16 words follow!
         unsigned char inst = (opcode << 4) | 0xf;
         Layout.push_back(inst);
         size_in_words -= 16;
      }
      if (size_in_words > 0) {
         // Note that value in imm. is one less that the actual
         // value. So, we subtract 1 away!
         unsigned char inst = (opcode << 4) | (size_in_words-1);
         Layout.push_back(inst);
      }
      if (residue_in_bytes > CharUnits::Zero()) {
         unsigned char inst =
         (BLOCK_LAYOUT_NON_OBJECT_BYTES << 4) | (residue_in_bytes.getQuantity()-1);
         Layout.push_back(inst);
      }
   }
   
   int e = Layout.size()-1;
   while (e >= 0) {
      unsigned char inst = Layout[e--];
      enum BLOCK_LAYOUT_OPCODE opcode = (enum BLOCK_LAYOUT_OPCODE) (inst >> 4);
      if (opcode == BLOCK_LAYOUT_NON_OBJECT_BYTES || opcode == BLOCK_LAYOUT_NON_OBJECT_WORDS)
         Layout.pop_back();
      else
         break;
   }
   
   uint64_t Result = InlineLayoutInstruction(Layout);
   if (Result != 0) {
      // Block variable layout instruction has been inlined.
      if (CGM.getLangOpts().ObjCGCBitmapPrint) {
         if (ComputeByrefLayout)
            printf("\n Inline instruction for BYREF variable layout: ");
         else
            printf("\n Inline instruction for block variable layout: ");
         printf("0x0%" PRIx64 "\n", Result);
      }
      if (WordSizeInBytes == 8) {
         const llvm::APInt Instruction(64, Result);
         return llvm::Constant::getIntegerValue(CGM.Int64Ty, Instruction);
      }
      else {
         const llvm::APInt Instruction(32, Result);
         return llvm::Constant::getIntegerValue(CGM.Int32Ty, Instruction);
      }
   }
   
   unsigned char inst = (BLOCK_LAYOUT_OPERATOR << 4) | 0;
   Layout.push_back(inst);
   std::string BitMap;
   for (unsigned i = 0, e = Layout.size(); i != e; i++)
      BitMap += Layout[i];
   
   if (CGM.getLangOpts().ObjCGCBitmapPrint) {
      if (ComputeByrefLayout)
         printf("\n BYREF variable layout: ");
      else
         printf("\n block variable layout: ");
      for (unsigned i = 0, e = BitMap.size(); i != e; i++) {
         unsigned char inst = BitMap[i];
         enum BLOCK_LAYOUT_OPCODE opcode = (enum BLOCK_LAYOUT_OPCODE) (inst >> 4);
         unsigned delta = 1;
         switch (opcode) {
            case BLOCK_LAYOUT_OPERATOR:
               printf("BL_OPERATOR:");
               delta = 0;
               break;
            case BLOCK_LAYOUT_NON_OBJECT_BYTES:
               printf("BL_NON_OBJECT_BYTES:");
               break;
            case BLOCK_LAYOUT_NON_OBJECT_WORDS:
               printf("BL_NON_OBJECT_WORD:");
               break;
            case BLOCK_LAYOUT_STRONG:
               printf("BL_STRONG:");
               break;
            case BLOCK_LAYOUT_BYREF:
               printf("BL_BYREF:");
               break;
            case BLOCK_LAYOUT_WEAK:
               printf("BL_WEAK:");
               break;
            case BLOCK_LAYOUT_UNRETAINED:
               printf("BL_UNRETAINED:");
               break;
         }
         // Actual value of word count is one more that what is in the imm.
         // field of the instruction
         printf("%d", (inst & 0xf) + delta);
         if (i < e-1)
            printf(", ");
         else
            printf("\n");
      }
   }
   
   llvm::GlobalVariable *Entry = CreateMetadataVar(
                                                   "OBJC_CLASS_NAME_",
                                                   llvm::ConstantDataArray::getString(VMContext, BitMap, false),
                                                   "__TEXT,__objc_classname,cstring_literals", 1, true);
   return getConstantGEP(VMContext, Entry, 0, 0);
}

llvm::Constant *CGObjCCommonMulleRuntime::BuildRCBlockLayout(CodeGenModule &CGM,
                                                    const CGBlockInfo &blockInfo) {
   assert(CGM.getLangOpts().getGC() == LangOptions::NonGC);
   
   RunSkipBlockVars.clear();
   bool hasUnion = false;
   
   unsigned WordSizeInBits = CGM.getTarget().getPointerWidth(0);
   unsigned ByteSizeInBits = CGM.getTarget().getCharWidth();
   unsigned WordSizeInBytes = WordSizeInBits/ByteSizeInBits;
   
   const BlockDecl *blockDecl = blockInfo.getBlockDecl();
   
   // Calculate the basic layout of the block structure.
   const llvm::StructLayout *layout =
   CGM.getDataLayout().getStructLayout(blockInfo.StructureType);
   
   // Ignore the optional 'this' capture: C++ objects are not assumed
   // to be GC'ed.
   if (blockInfo.BlockHeaderForcedGapSize != CharUnits::Zero())
      UpdateRunSkipBlockVars(false, Qualifiers::OCL_None,
                             blockInfo.BlockHeaderForcedGapOffset,
                             blockInfo.BlockHeaderForcedGapSize);
   // Walk the captured variables.
   for (const auto &CI : blockDecl->captures()) {
      const VarDecl *variable = CI.getVariable();
      QualType type = variable->getType();
      
      const CGBlockInfo::Capture &capture = blockInfo.getCapture(variable);
      
      // Ignore constant captures.
      if (capture.isConstant()) continue;
      
      CharUnits fieldOffset =
      CharUnits::fromQuantity(layout->getElementOffset(capture.getIndex()));
      
      assert(!type->isArrayType() && "array variable should not be caught");
      if (!CI.isByRef())
         if (const RecordType *record = type->getAs<RecordType>()) {
            BuildRCBlockVarRecordLayout(record, fieldOffset, hasUnion);
            continue;
         }
      CharUnits fieldSize;
      if (CI.isByRef())
         fieldSize = CharUnits::fromQuantity(WordSizeInBytes);
      else
         fieldSize = CGM.getContext().getTypeSizeInChars(type);
      UpdateRunSkipBlockVars(CI.isByRef(), getBlockCaptureLifetime(type, false),
                             fieldOffset, fieldSize);
   }
   return getBitmapBlockLayout(false);
}


llvm::Constant *CGObjCCommonMulleRuntime::BuildByrefLayout(CodeGen::CodeGenModule &CGM,
                                                  QualType T) {
   assert(CGM.getLangOpts().getGC() == LangOptions::NonGC);
   assert(!T->isArrayType() && "__block array variable should not be caught");
   CharUnits fieldOffset;
   RunSkipBlockVars.clear();
   bool hasUnion = false;
   if (const RecordType *record = T->getAs<RecordType>()) {
      BuildRCBlockVarRecordLayout(record, fieldOffset, hasUnion, true /*ByrefLayout */);
      llvm::Constant *Result = getBitmapBlockLayout(true);
      return Result;
   }
   llvm::Constant *nullPtr = llvm::Constant::getNullValue(CGM.Int8PtrTy);
   return nullPtr;
}

# pragma mark -
# pragma mark protocols

llvm::Value *CGObjCMulleRuntime::GenerateProtocolRef(CodeGenFunction &CGF,
                                            const ObjCProtocolDecl *PD) {
   // FIXME: I don't understand why gcc generates this, or where it is
   // resolved. Investigate. Its also wasteful to look this up over and over.
   LazySymbols.insert(&CGM.getContext().Idents.get("Protocol"));
   
   return llvm::ConstantExpr::getBitCast(GetProtocolRef(PD),
                                         ObjCTypes.getExternalProtocolPtrTy());
}

void CGObjCCommonMulleRuntime::GenerateProtocol(const ObjCProtocolDecl *PD) {
   // FIXME: We shouldn't need this, the protocol decl should contain enough
   // information to tell us whether this was a declaration or a definition.
   DefinedProtocols.insert(PD->getIdentifier());
   
   // If we have generated a forward reference to this protocol, emit
   // it now. Otherwise do nothing, the protocol objects are lazily
   // emitted.
   if (Protocols.count(PD->getIdentifier()))
      GetOrEmitProtocol(PD);
}

llvm::Constant *CGObjCCommonMulleRuntime::GetProtocolRef(const ObjCProtocolDecl *PD) {
   if (DefinedProtocols.count(PD->getIdentifier()))
      return GetOrEmitProtocol(PD);
   
   return GetOrEmitProtocolRef(PD);
}



llvm::Constant *CGObjCMulleRuntime::GetOrEmitProtocol(const ObjCProtocolDecl *PD)
{
   StringRef    sref( PD->getIdentifier()->getName());
   
   return( HashConstantForString( sref));
}


llvm::Constant *CGObjCMulleRuntime::GetOrEmitProtocolRef(const ObjCProtocolDecl *PD)
{
   StringRef    sref( PD->getIdentifier()->getName());
   
   return( HashConstantForString( sref));
}


/*
 just a list of protocol IDs,
 which gets stuck on a category or on a class
 */
llvm::Constant *
CGObjCMulleRuntime::EmitProtocolIDList(Twine Name,
                            ObjCProtocolDecl::protocol_iterator begin,
                            ObjCProtocolDecl::protocol_iterator end)
{
   SmallVector<llvm::Constant *, 16> ProtocolRefs;
   
   for (; begin != end; ++begin)
      ProtocolRefs.push_back(GetProtocolRef(*begin));
   
   // Just return null for empty protocol lists
   if (ProtocolRefs.empty())
      return llvm::Constant::getNullValue(ObjCTypes.ProtocolIDPtrTy);
   
   // This list is null terminated.
   ProtocolRefs.push_back(llvm::Constant::getNullValue(ObjCTypes.ProtocolIDTy));
   
   llvm::Constant *Values[ 1];
   Values[ 0] = llvm::ConstantArray::get(llvm::ArrayType::get(ObjCTypes.ProtocolIDTy,
                                                              ProtocolRefs.size()),
                                         ProtocolRefs);
   
   llvm::Constant       *Init = llvm::ConstantStruct::getAnon(Values);
   llvm::GlobalVariable *GV =
   CreateMetadataVar(Name, Init, "__DATA,regular,no_dead_strip",
                     4, false);
   return llvm::ConstantExpr::getBitCast( GV, ObjCTypes.ProtocolIDPtrTy);
}


void CGObjCCommonMulleRuntime::
PushProtocolProperties(llvm::SmallPtrSet<const IdentifierInfo*,16> &PropertySet,
                       SmallVectorImpl<llvm::Constant *> &Properties,
                       const Decl *Container,
                       const ObjCProtocolDecl *Proto,
                       const ObjCCommonTypesHelper &ObjCTypes) {
   for (const auto *P : Proto->protocols())
      PushProtocolProperties(PropertySet, Properties, Container, P, ObjCTypes);
   for (const auto *PD : Proto->properties()) {
      if (!PropertySet.insert(PD->getIdentifier()).second)
         continue;
      llvm::Constant *Prop[] = {
         GetPropertyName(PD->getIdentifier()),
         GetPropertyTypeString(PD, Container)
      };
      Properties.push_back(llvm::ConstantStruct::get(ObjCTypes.PropertyTy, Prop));
   }
}

/*
 struct _objc_property {
 const char * const name;
 const char * const attributes;
 };
 
 struct _objc_property_list {
 uint32_t entsize; // sizeof (struct _objc_property)
 uint32_t prop_count;
 struct _objc_property[prop_count];
 };
 */
llvm::Constant *CGObjCCommonMulleRuntime::EmitPropertyList(Twine Name,
                                                  const Decl *Container,
                                                  const ObjCContainerDecl *OCD,
                                                  const ObjCCommonTypesHelper &ObjCTypes) {
   SmallVector<llvm::Constant *, 16> Properties;
   llvm::SmallPtrSet<const IdentifierInfo*, 16> PropertySet;
   for (const auto *PD : OCD->properties()) {
      PropertySet.insert(PD->getIdentifier());
      llvm::Constant *Prop[] = {
         GetPropertyName(PD->getIdentifier()),
         GetPropertyTypeString(PD, Container)
      };
      Properties.push_back(llvm::ConstantStruct::get(ObjCTypes.PropertyTy,
                                                     Prop));
   }
   if (const ObjCInterfaceDecl *OID = dyn_cast<ObjCInterfaceDecl>(OCD)) {
      for (const auto *P : OID->all_referenced_protocols())
         PushProtocolProperties(PropertySet, Properties, Container, P, ObjCTypes);
   }
   else if (const ObjCCategoryDecl *CD = dyn_cast<ObjCCategoryDecl>(OCD)) {
      for (const auto *P : CD->protocols())
         PushProtocolProperties(PropertySet, Properties, Container, P, ObjCTypes);
   }
   
   // Return null for empty list.
   if (Properties.empty())
      return llvm::Constant::getNullValue(ObjCTypes.PropertyListPtrTy);
   
   unsigned PropertySize =
   CGM.getDataLayout().getTypeAllocSize(ObjCTypes.PropertyTy);
   llvm::Constant *Values[3];
   Values[0] = llvm::ConstantInt::get(ObjCTypes.IntTy, PropertySize);
   Values[1] = llvm::ConstantInt::get(ObjCTypes.IntTy, Properties.size());
   llvm::ArrayType *AT = llvm::ArrayType::get(ObjCTypes.PropertyTy,
                                              Properties.size());
   Values[2] = llvm::ConstantArray::get(AT, Properties);
   llvm::Constant *Init = llvm::ConstantStruct::getAnon(Values);
   
   llvm::GlobalVariable *GV =
   CreateMetadataVar(Name, Init,
                     (ObjCABI == 2) ? "__DATA, __objc_const" :
                     "__DATA,__property,regular,no_dead_strip",
                     (ObjCABI == 2) ? 8 : 4,
                     true);
   return llvm::ConstantExpr::getBitCast(GV, ObjCTypes.PropertyListPtrTy);
}

llvm::Constant *
CGObjCCommonMulleRuntime::EmitProtocolMethodTypes(Twine Name,
                                         ArrayRef<llvm::Constant*> MethodTypes,
                                         const ObjCCommonTypesHelper &ObjCTypes) {
   // Return null for empty list.
   if (MethodTypes.empty())
      return llvm::Constant::getNullValue(ObjCTypes.Int8PtrPtrTy);
   
   llvm::ArrayType *AT = llvm::ArrayType::get(ObjCTypes.Int8PtrTy,
                                              MethodTypes.size());
   llvm::Constant *Init = llvm::ConstantArray::get(AT, MethodTypes);
   
   llvm::GlobalVariable *GV = CreateMetadataVar(
                                                Name, Init, (ObjCABI == 2) ? "__DATA, __objc_const" : StringRef(),
                                                (ObjCABI == 2) ? 8 : 4, true);
   return llvm::ConstantExpr::getBitCast(GV, ObjCTypes.Int8PtrPtrTy);
}

# pragma mark -
# pragma mark method descriptions

/*
 struct objc_method_description_list {
 int count;
 struct objc_method_description list[];
 };
 */
llvm::Constant *
CGObjCMulleRuntime::GetMethodDescriptionConstant(const ObjCMethodDecl *MD) {
   llvm::Constant *Desc[] = {
      llvm::ConstantExpr::getBitCast(GetMethodVarName(MD->getSelector()),
                                     ObjCTypes.SelectorIDTy),
      GetMethodVarType(MD)
   };
   if (!Desc[1])
      return nullptr;
   
   return llvm::ConstantStruct::get(ObjCTypes.MethodDescriptionTy,
                                    Desc);
}

llvm::Constant *
CGObjCMulleRuntime::EmitMethodDescList(Twine Name, const char *Section,
                              ArrayRef<llvm::Constant*> Methods) {
   // Return null for empty list.
   if (Methods.empty())
      return llvm::Constant::getNullValue(ObjCTypes.MethodDescriptionListPtrTy);
   
   llvm::Constant *Values[2];
   Values[0] = llvm::ConstantInt::get(ObjCTypes.IntTy, Methods.size());
   llvm::ArrayType *AT = llvm::ArrayType::get(ObjCTypes.MethodDescriptionTy,
                                              Methods.size());
   Values[1] = llvm::ConstantArray::get(AT, Methods);
   llvm::Constant *Init = llvm::ConstantStruct::getAnon(Values);
   
   llvm::GlobalVariable *GV = CreateMetadataVar(Name, Init, Section, 4, true);
   return llvm::ConstantExpr::getBitCast(GV,
                                         ObjCTypes.MethodDescriptionListPtrTy);
}

/*
 struct _objc_category {
 char *category_name;
 char *class_name;
 struct _objc_method_list *instance_methods;
 struct _objc_method_list *class_methods;
 struct _objc_protocol_list *protocols;
 uint32_t size; // <rdar://4585769>
 struct _objc_property_list *instance_properties;
 };
 */
void CGObjCMulleRuntime::GenerateCategory(const ObjCCategoryImplDecl *OCD) {
   unsigned Size = CGM.getDataLayout().getTypeAllocSize(ObjCTypes.CategoryTy);
   
   // FIXME: This is poor design, the OCD should have a pointer to the category
   // decl. Additionally, note that Category can be null for the @implementation
   // w/o an @interface case. Sema should just create one for us as it does for
   // @implementation so everyone else can live life under a clear blue sky.
   const ObjCInterfaceDecl *Interface = OCD->getClassInterface();
   const ObjCCategoryDecl *Category =
   Interface->FindCategoryDeclaration(OCD->getIdentifier());
   
   SmallString<256> ExtName;
   llvm::raw_svector_ostream(ExtName) << Interface->getName() << '_'
   << OCD->getName();
   
   SmallVector<llvm::Constant *, 16> InstanceMethods, ClassMethods;
   for (const auto *I : OCD->instance_methods())
      // Instance methods should always be defined.
      InstanceMethods.push_back(GetMethodConstant(I));
   
   for (const auto *I : OCD->class_methods())
      // Class methods should always be defined.
      ClassMethods.push_back(GetMethodConstant(I));
   
   llvm::Constant *Values[7];
   Values[0] = GetClassName(OCD->getName());
   Values[1] = GetClassName(Interface->getObjCRuntimeNameAsString());
   LazySymbols.insert(Interface->getIdentifier());
   Values[2] = EmitMethodList("OBJC_CATEGORY_INSTANCE_METHODS_" + ExtName.str(),
                              "__DATA,__cat_inst_meth,regular,no_dead_strip",
                              InstanceMethods);
   Values[3] = EmitMethodList("OBJC_CATEGORY_CLASS_METHODS_" + ExtName.str(),
                              "__DATA,__cat_cls_meth,regular,no_dead_strip",
                              ClassMethods);
   if (Category) {
      Values[4] =
      EmitProtocolIDList("OBJC_CATEGORY_PROTOCOLS_" + ExtName.str(),
                          Category->protocol_begin(), Category->protocol_end());
   } else {
      Values[4] = llvm::Constant::getNullValue(ObjCTypes.ProtocolListPtrTy);
   }
   Values[5] = llvm::ConstantInt::get(ObjCTypes.IntTy, Size);
   
   // If there is no category @interface then there can be no properties.
   if (Category) {
      Values[6] = EmitPropertyList("\01l_OBJC_$_PROP_LIST_" + ExtName.str(),
                                   OCD, Category, ObjCTypes);
   } else {
      Values[6] = llvm::Constant::getNullValue(ObjCTypes.PropertyListPtrTy);
   }
   
   llvm::Constant *Init = llvm::ConstantStruct::get(ObjCTypes.CategoryTy,
                                                    Values);
   
   llvm::GlobalVariable *GV =
   CreateMetadataVar("OBJC_CATEGORY_" + ExtName.str(), Init,
                     "__DATA,__category,regular,no_dead_strip", 4, true);
   DefinedCategories.push_back(GV);
   DefinedCategoryNames.insert(ExtName.str());
   // method definition entries must be clear for next implementation.
   MethodDefinitions.clear();
}


# pragma mark -
# pragma mark class


enum FragileClassFlags {
   FragileABI_Class_Factory                 = 0x00001,
   FragileABI_Class_Meta                    = 0x00002,
   FragileABI_Class_HasCXXStructors         = 0x02000,
   FragileABI_Class_Hidden                  = 0x20000
};


/*
 //   struct _mulle_objc_load_class
 //   {
 //      mulle_objc_class_id_t            class_id;
 //      char                             *class_name;
 //      size_t                           instance_size;
 //
 //      mulle_objc_class_id_t            superclass_unique_id;
 //      char                             *superclass_name;
 //
 //      struct _mulle_objc_method_list   *class_methods;
 //      struct _mulle_objc_method_list   *instance_methods;
 //
 //      mulle_objc_protocol_id_t         *protocol_unique_ids;
 //   };
 */
void CGObjCMulleRuntime::GenerateClass(const ObjCImplementationDecl *ID) {
   DefinedSymbols.insert(ID->getIdentifier());
   
   std::string ClassName = ID->getNameAsString();
   // FIXME: Gross
   ObjCInterfaceDecl *Interface =
   const_cast<ObjCInterfaceDecl*>(ID->getClassInterface());
   llvm::Constant *Protocols =
   EmitProtocolIDList("OBJC_CLASS_PROTOCOLS_" + ID->getName(),
                    Interface->all_referenced_protocol_begin(),
                    Interface->all_referenced_protocol_end());
   unsigned Flags = FragileABI_Class_Factory;
   if (ID->hasNonZeroConstructors() || ID->hasDestructors())
      Flags |= FragileABI_Class_HasCXXStructors;
   unsigned Size =
   CGM.getContext().getASTObjCImplementationLayout(ID).getSize().getQuantity();
   
   // FIXME: Set CXX-structors flag.
   if (ID->getClassInterface()->getVisibility() == HiddenVisibility)
      Flags |= FragileABI_Class_Hidden;
   
   SmallVector<llvm::Constant *, 16> InstanceMethods, ClassMethods;
   for (const auto *I : ID->instance_methods())
      // Instance methods should always be defined.
      InstanceMethods.push_back(GetMethodConstant(I));
   
   for (const auto *I : ID->class_methods())
      // Class methods should always be defined.
      ClassMethods.push_back(GetMethodConstant(I));
   
   for (const auto *PID : ID->property_impls()) {
      if (PID->getPropertyImplementation() == ObjCPropertyImplDecl::Synthesize) {
         ObjCPropertyDecl *PD = PID->getPropertyDecl();
         
         if (ObjCMethodDecl *MD = PD->getGetterMethodDecl())
            if (llvm::Constant *C = GetMethodConstant(MD))
               InstanceMethods.push_back(C);
         if (ObjCMethodDecl *MD = PD->getSetterMethodDecl())
            if (llvm::Constant *C = GetMethodConstant(MD))
               InstanceMethods.push_back(C);
      }
   }
   
  
   //   struct _mulle_objc_load_class
   //   {
   //      mulle_objc_class_id_t            class_id;
   //      char                             *class_name;
   //
   //      mulle_objc_class_id_t            superclass_unique_id;
   //      char                             *superclass_name;
   //
   //      size_t                           instance_size;
   //
   //      struct _mulle_objc_method_list   *class_methods;
   //      struct _mulle_objc_method_list   *instance_methods;
   //
   //      mulle_objc_protocol_id_t         *protocol_unique_ids;
   //   };
   
   llvm::Constant *Values[8];
   
   ObjCInterfaceDecl *Super = Interface->getSuperClass();

   Values[ 0] =  llvm::ConstantExpr::getBitCast( HashConstantForString( ID->getObjCRuntimeNameAsString()), ObjCTypes.ClassIDTy);
   Values[ 1] = GetClassName(ID->getObjCRuntimeNameAsString());

   if( Super)
   {
      Values[ 2] = llvm::ConstantExpr::getBitCast( HashConstantForString( Super->getObjCRuntimeNameAsString()), ObjCTypes.ClassIDTy);
      Values[ 3] = GetClassName( Super->getObjCRuntimeNameAsString());
   }
   else
   {
      Values[ 2] = llvm::Constant::getNullValue(ObjCTypes.ClassIDTy);
      Values[ 3] = llvm::Constant::getNullValue(ObjCTypes.Int8PtrTy);
   }
   
   Values[ 4] = llvm::ConstantInt::get(ObjCTypes.LongTy, Size);
   
   Values[ 5] = EmitMethodList("OBJC_CLASS_METHODS_" + ID->getNameAsString(),
                                 "_DATA,__cls_meth,regular,no_dead_strip", ClassMethods);
   Values[ 6] = EmitMethodList("OBJC_CLASS_METHODS_" + ID->getNameAsString(),
                               "_DATA,__inst_meth,regular,no_dead_strip", InstanceMethods);
   Values[ 7] = Protocols;

   llvm::Constant *Init = llvm::ConstantStruct::get(ObjCTypes.ClassTy,
                                                    Values);
   std::string Name("OBJC_CLASS_");
   Name += ClassName;

   // cargo cult programming
   const char *Section = "__DATA,__class,regular,no_dead_strip";
   // Check for a forward reference.
   llvm::GlobalVariable *GV = CGM.getModule().getGlobalVariable(Name, true);
   if (GV) {
      assert(GV->getType()->getElementType() == ObjCTypes.ClassTy &&
             "Forward metaclass reference has incorrect type.");
      GV->setInitializer(Init);
      GV->setSection(Section);
      GV->setAlignment(4);
      CGM.addCompilerUsedGlobal(GV);
   } else
      GV = CreateMetadataVar(Name, Init, Section, 4, true);

   DefinedClasses.push_back(GV);
   ImplementedClasses.push_back(Interface);
   
   // method definition entries must be clear for next implementation.
   MethodDefinitions.clear();
}



/*
 struct objc_ivar {
 char *ivar_name;
 char *ivar_type;
 int ivar_offset;
 };
 
 struct objc_ivar_list {
 int ivar_count;
 struct objc_ivar list[count];
 };
 */
llvm::Constant *CGObjCMulleRuntime::EmitIvarList(const ObjCImplementationDecl *ID,
                                        bool ForClass) {
   std::vector<llvm::Constant*> Ivars;
   
   // When emitting the root class GCC emits ivar entries for the
   // actual class structure. It is not clear if we need to follow this
   // behavior; for now lets try and get away with not doing it. If so,
   // the cleanest solution would be to make up an ObjCInterfaceDecl
   // for the class.
   if (ForClass)
      return llvm::Constant::getNullValue(ObjCTypes.IvarListPtrTy);
   
   const ObjCInterfaceDecl *OID = ID->getClassInterface();
   
   for (const ObjCIvarDecl *IVD = OID->all_declared_ivar_begin();
        IVD; IVD = IVD->getNextIvar()) {
      // Ignore unnamed bit-fields.
      if (!IVD->getDeclName())
         continue;
      llvm::Constant *Ivar[] = {
         GetMethodVarName(IVD->getIdentifier()),
         GetMethodVarType(IVD),
         llvm::ConstantInt::get(ObjCTypes.IntTy,
                                ComputeIvarBaseOffset(CGM, OID, IVD))
      };
      Ivars.push_back(llvm::ConstantStruct::get(ObjCTypes.IvarTy, Ivar));
   }
   
   // Return null for empty list.
   if (Ivars.empty())
      return llvm::Constant::getNullValue(ObjCTypes.IvarListPtrTy);
   
   llvm::Constant *Values[2];
   Values[0] = llvm::ConstantInt::get(ObjCTypes.IntTy, Ivars.size());
   llvm::ArrayType *AT = llvm::ArrayType::get(ObjCTypes.IvarTy,
                                              Ivars.size());
   Values[1] = llvm::ConstantArray::get(AT, Ivars);
   llvm::Constant *Init = llvm::ConstantStruct::getAnon(Values);
   
   llvm::GlobalVariable *GV;
   if (ForClass)
      GV =
      CreateMetadataVar("OBJC_CLASS_VARIABLES_" + ID->getName(), Init,
                        "__DATA,__class_vars,regular,no_dead_strip", 4, true);
   else
      GV = CreateMetadataVar("OBJC_INSTANCE_VARIABLES_" + ID->getName(), Init,
                             "__DATA,__instance_vars,regular,no_dead_strip", 4,
                             true);
   return llvm::ConstantExpr::getBitCast(GV, ObjCTypes.IvarListPtrTy);
}

/*
  struct _mulle_objc_method_descriptor
  {
      mulle_objc_method_id_t   method_id;
      char                     *name;
      char                     *signature;
      unsigned int             bits;
};

 struct objc_method_list {
 struct objc_method_list *obsolete;
 int count;
 struct objc_method methods_list[count];
 };
 */

/// GetMethodConstant - Return a struct objc_method constant for the
/// given method if it has been defined. The result is null if the
/// method has not been defined. The return value has type MethodPtrTy.
llvm::Constant *CGObjCMulleRuntime::GetMethodConstant(const ObjCMethodDecl *MD) {
   llvm::Function *Fn = GetMethodDefinition(MD);
   if (!Fn)
      return nullptr;
   
   llvm::Constant *Method[] = {
      llvm::ConstantExpr::getBitCast( HashConstantForString( MD->getSelector().getAsString()),
                                       ObjCTypes.SelectorIDTy),
      llvm::ConstantExpr::getBitCast(GetMethodVarName(MD->getSelector()),
                                     ObjCTypes.Int8PtrTy),
      GetMethodVarType(MD),
      llvm::ConstantInt::get(ObjCTypes.IntTy, 0),
      llvm::ConstantExpr::getBitCast(Fn, ObjCTypes.Int8PtrTy)
   };
   return llvm::ConstantStruct::get(ObjCTypes.MethodTy, Method);
}

llvm::Constant *CGObjCMulleRuntime::EmitMethodList(Twine Name,
                                          const char *Section,
                                          ArrayRef<llvm::Constant*> Methods) {
   // Return null for empty list.
   if (Methods.empty())
      return llvm::Constant::getNullValue(ObjCTypes.MethodListPtrTy);
   
   llvm::Constant *Values[3];
   Values[0] = llvm::Constant::getNullValue(ObjCTypes.Int8PtrTy);
   Values[1] = llvm::ConstantInt::get(ObjCTypes.IntTy, Methods.size());
   llvm::ArrayType *AT = llvm::ArrayType::get(ObjCTypes.MethodTy,
                                              Methods.size());
   Values[2] = llvm::ConstantArray::get(AT, Methods);
   llvm::Constant *Init = llvm::ConstantStruct::getAnon(Values);
   
   llvm::GlobalVariable *GV = CreateMetadataVar(Name, Init, Section, 4, true);
   return llvm::ConstantExpr::getBitCast(GV, ObjCTypes.MethodListPtrTy);
}

llvm::Function *CGObjCCommonMulleRuntime::GenerateMethod(const ObjCMethodDecl *OMD,
                                                const ObjCContainerDecl *CD) {
   SmallString<256> Name;
   GetNameForMethod(OMD, CD, Name);
   
   CodeGenTypes &Types = CGM.getTypes();
   llvm::FunctionType *MethodTy =
   Types.GetFunctionType(Types.arrangeObjCMethodDeclaration(OMD));
   llvm::Function *Method =
   llvm::Function::Create(MethodTy,
                          llvm::GlobalValue::InternalLinkage,
                          Name.str(),
                          &CGM.getModule());
   MethodDefinitions.insert(std::make_pair(OMD, Method));
   
   return Method;
}

llvm::GlobalVariable *CGObjCCommonMulleRuntime::CreateMetadataVar(Twine Name,
                                                         llvm::Constant *Init,
                                                         StringRef Section,
                                                         unsigned Align,
                                                         bool AddToUsed) {
   llvm::Type *Ty = Init->getType();
   llvm::GlobalVariable *GV =
   new llvm::GlobalVariable(CGM.getModule(), Ty, false,
                            llvm::GlobalValue::PrivateLinkage, Init, Name);
   if (!Section.empty())
      GV->setSection(Section);
   if (Align)
      GV->setAlignment(Align);
   if (AddToUsed)
      CGM.addCompilerUsedGlobal(GV);
   return GV;
}

llvm::Function *CGObjCMulleRuntime::ModuleInitFunction() {
   // Abuse this interface function as a place to finalize.
   // Although it's called init, it's being called during
   // CodeGenModule::Release so it's certainly not too early (but maybe too
   // late ?)
   FinishModule();
   return nullptr;
}

llvm::Constant *CGObjCMulleRuntime::GetPropertyGetFunction() {
   return ObjCTypes.getGetPropertyFn();
}

llvm::Constant *CGObjCMulleRuntime::GetPropertySetFunction() {
   return ObjCTypes.getSetPropertyFn();
}

llvm::Constant *CGObjCMulleRuntime::GetOptimizedPropertySetFunction(bool atomic,
                                                           bool copy) {
   return ObjCTypes.getOptimizedSetPropertyFn(atomic, copy);
}

llvm::Constant *CGObjCMulleRuntime::GetGetStructFunction() {
   return ObjCTypes.getCopyStructFn();
}
llvm::Constant *CGObjCMulleRuntime::GetSetStructFunction() {
   return ObjCTypes.getCopyStructFn();
}

llvm::Constant *CGObjCMulleRuntime::GetCppAtomicObjectGetFunction() {
   return ObjCTypes.getCppAtomicObjectFunction();
}
llvm::Constant *CGObjCMulleRuntime::GetCppAtomicObjectSetFunction() {
   return ObjCTypes.getCppAtomicObjectFunction();
}

llvm::Constant *CGObjCMulleRuntime::EnumerationMutationFunction() {
   return ObjCTypes.getEnumerationMutationFn();
}

void CGObjCMulleRuntime::EmitTryStmt(CodeGenFunction &CGF, const ObjCAtTryStmt &S) {
   return EmitTryOrSynchronizedStmt(CGF, S);
}

void CGObjCMulleRuntime::EmitSynchronizedStmt(CodeGenFunction &CGF,
                                     const ObjCAtSynchronizedStmt &S) {
   return EmitTryOrSynchronizedStmt(CGF, S);
}

namespace {
   struct PerformFragileFinally : EHScopeStack::Cleanup {
      const Stmt &S;
      llvm::Value *SyncArgSlot;
      llvm::Value *CallTryExitVar;
      llvm::Value *ExceptionData;
      ObjCTypesHelper &ObjCTypes;
      PerformFragileFinally(const Stmt *S,
                            llvm::Value *SyncArgSlot,
                            llvm::Value *CallTryExitVar,
                            llvm::Value *ExceptionData,
                            ObjCTypesHelper *ObjCTypes)
      : S(*S), SyncArgSlot(SyncArgSlot), CallTryExitVar(CallTryExitVar),
      ExceptionData(ExceptionData), ObjCTypes(*ObjCTypes) {}
      
      void Emit(CodeGenFunction &CGF, Flags flags) override {
         // Check whether we need to call objc_exception_try_exit.
         // In optimized code, this branch will always be folded.
         llvm::BasicBlock *FinallyCallExit =
         CGF.createBasicBlock("finally.call_exit");
         llvm::BasicBlock *FinallyNoCallExit =
         CGF.createBasicBlock("finally.no_call_exit");
         CGF.Builder.CreateCondBr(CGF.Builder.CreateLoad(CallTryExitVar),
                                  FinallyCallExit, FinallyNoCallExit);
         
         CGF.EmitBlock(FinallyCallExit);
         CGF.EmitNounwindRuntimeCall(ObjCTypes.getExceptionTryExitFn(),
                                     ExceptionData);
         
         CGF.EmitBlock(FinallyNoCallExit);
         
         if (isa<ObjCAtTryStmt>(S)) {
            if (const ObjCAtFinallyStmt* FinallyStmt =
                cast<ObjCAtTryStmt>(S).getFinallyStmt()) {
               // Don't try to do the @finally if this is an EH cleanup.
               if (flags.isForEHCleanup()) return;
               
               // Save the current cleanup destination in case there's
               // control flow inside the finally statement.
               llvm::Value *CurCleanupDest =
               CGF.Builder.CreateLoad(CGF.getNormalCleanupDestSlot());
               
               CGF.EmitStmt(FinallyStmt->getFinallyBody());
               
               if (CGF.HaveInsertPoint()) {
                  CGF.Builder.CreateStore(CurCleanupDest,
                                          CGF.getNormalCleanupDestSlot());
               } else {
                  // Currently, the end of the cleanup must always exist.
                  CGF.EnsureInsertPoint();
               }
            }
         } else {
            // Emit objc_sync_exit(expr); as finally's sole statement for
            // @synchronized.
            llvm::Value *SyncArg = CGF.Builder.CreateLoad(SyncArgSlot);
            CGF.EmitNounwindRuntimeCall(ObjCTypes.getSyncExitFn(), SyncArg);
         }
      }
   };
   
   class FragileHazards {
      CodeGenFunction &CGF;
      SmallVector<llvm::Value*, 20> Locals;
      llvm::DenseSet<llvm::BasicBlock*> BlocksBeforeTry;
      
      llvm::InlineAsm *ReadHazard;
      llvm::InlineAsm *WriteHazard;
      
      llvm::FunctionType *GetAsmFnType();
      
      void collectLocals();
      void emitReadHazard(CGBuilderTy &Builder);
      
   public:
      FragileHazards(CodeGenFunction &CGF);
      
      void emitWriteHazard();
      void emitHazardsInNewBlocks();
   };
}

/// Create the fragile-ABI read and write hazards based on the current
/// state of the function, which is presumed to be immediately prior
/// to a @try block.  These hazards are used to maintain correct
/// semantics in the face of optimization and the fragile ABI's
/// cavalier use of setjmp/longjmp.
FragileHazards::FragileHazards(CodeGenFunction &CGF) : CGF(CGF) {
   collectLocals();
   
   if (Locals.empty()) return;
   
   // Collect all the blocks in the function.
   for (llvm::Function::iterator
        I = CGF.CurFn->begin(), E = CGF.CurFn->end(); I != E; ++I)
      BlocksBeforeTry.insert(&*I);
   
   llvm::FunctionType *AsmFnTy = GetAsmFnType();
   
   // Create a read hazard for the allocas.  This inhibits dead-store
   // optimizations and forces the values to memory.  This hazard is
   // inserted before any 'throwing' calls in the protected scope to
   // reflect the possibility that the variables might be read from the
   // catch block if the call throws.
   {
      std::string Constraint;
      for (unsigned I = 0, E = Locals.size(); I != E; ++I) {
         if (I) Constraint += ',';
         Constraint += "*m";
      }
      
      ReadHazard = llvm::InlineAsm::get(AsmFnTy, "", Constraint, true, false);
   }
   
   // Create a write hazard for the allocas.  This inhibits folding
   // loads across the hazard.  This hazard is inserted at the
   // beginning of the catch path to reflect the possibility that the
   // variables might have been written within the protected scope.
   {
      std::string Constraint;
      for (unsigned I = 0, E = Locals.size(); I != E; ++I) {
         if (I) Constraint += ',';
         Constraint += "=*m";
      }
      
      WriteHazard = llvm::InlineAsm::get(AsmFnTy, "", Constraint, true, false);
   }
}

/// Emit a write hazard at the current location.
void FragileHazards::emitWriteHazard() {
   if (Locals.empty()) return;
   
   CGF.EmitNounwindRuntimeCall(WriteHazard, Locals);
}

void FragileHazards::emitReadHazard(CGBuilderTy &Builder) {
   assert(!Locals.empty());
   llvm::CallInst *call = Builder.CreateCall(ReadHazard, Locals);
   call->setDoesNotThrow();
   call->setCallingConv(CGF.getRuntimeCC());
}

/// Emit read hazards in all the protected blocks, i.e. all the blocks
/// which have been inserted since the beginning of the try.
void FragileHazards::emitHazardsInNewBlocks() {
   if (Locals.empty()) return;
   
   CGBuilderTy Builder(CGF.getLLVMContext());
   
   // Iterate through all blocks, skipping those prior to the try.
   for (llvm::Function::iterator
        FI = CGF.CurFn->begin(), FE = CGF.CurFn->end(); FI != FE; ++FI) {
      llvm::BasicBlock &BB = *FI;
      if (BlocksBeforeTry.count(&BB)) continue;
      
      // Walk through all the calls in the block.
      for (llvm::BasicBlock::iterator
           BI = BB.begin(), BE = BB.end(); BI != BE; ++BI) {
         llvm::Instruction &I = *BI;
         
         // Ignore instructions that aren't non-intrinsic calls.
         // These are the only calls that can possibly call longjmp.
         if (!isa<llvm::CallInst>(I) && !isa<llvm::InvokeInst>(I)) continue;
         if (isa<llvm::IntrinsicInst>(I))
            continue;
         
         // Ignore call sites marked nounwind.  This may be questionable,
         // since 'nounwind' doesn't necessarily mean 'does not call longjmp'.
         llvm::CallSite CS(&I);
         if (CS.doesNotThrow()) continue;
         
         // Insert a read hazard before the call.  This will ensure that
         // any writes to the locals are performed before making the
         // call.  If the call throws, then this is sufficient to
         // guarantee correctness as long as it doesn't also write to any
         // locals.
         Builder.SetInsertPoint(&BB, BI);
         emitReadHazard(Builder);
      }
   }
}

static void addIfPresent(llvm::DenseSet<llvm::Value*> &S, llvm::Value *V) {
   if (V) S.insert(V);
}

void FragileHazards::collectLocals() {
   // Compute a set of allocas to ignore.
   llvm::DenseSet<llvm::Value*> AllocasToIgnore;
   addIfPresent(AllocasToIgnore, CGF.ReturnValue);
   addIfPresent(AllocasToIgnore, CGF.NormalCleanupDest);
   
   // Collect all the allocas currently in the function.  This is
   // probably way too aggressive.
   llvm::BasicBlock &Entry = CGF.CurFn->getEntryBlock();
   for (llvm::BasicBlock::iterator
        I = Entry.begin(), E = Entry.end(); I != E; ++I)
      if (isa<llvm::AllocaInst>(*I) && !AllocasToIgnore.count(&*I))
         Locals.push_back(&*I);
}

llvm::FunctionType *FragileHazards::GetAsmFnType() {
   SmallVector<llvm::Type *, 16> tys(Locals.size());
   for (unsigned i = 0, e = Locals.size(); i != e; ++i)
      tys[i] = Locals[i]->getType();
   return llvm::FunctionType::get(CGF.VoidTy, tys, false);
}

/*
 
 Objective-C setjmp-longjmp (sjlj) Exception Handling
 --
 
 A catch buffer is a setjmp buffer plus:
 - a pointer to the exception that was caught
 - a pointer to the previous exception data buffer
 - two pointers of reserved storage
 Therefore catch buffers form a stack, with a pointer to the top
 of the stack kept in thread-local storage.
 
 objc_exception_try_enter pushes a catch buffer onto the EH stack.
 objc_exception_try_exit pops the given catch buffer, which is
 required to be the top of the EH stack.
 objc_exception_throw pops the top of the EH stack, writes the
 thrown exception into the appropriate field, and longjmps
 to the setjmp buffer.  It crashes the process (with a printf
 and an abort()) if there are no catch buffers on the stack.
 objc_exception_extract just reads the exception pointer out of the
 catch buffer.
 
 There's no reason an implementation couldn't use a light-weight
 setjmp here --- something like __builtin_setjmp, but API-compatible
 with the heavyweight setjmp.  This will be more important if we ever
 want to implement correct ObjC/C++ exception interactions for the
 fragile ABI.
 
 Note that for this use of setjmp/longjmp to be correct, we may need
 to mark some local variables volatile: if a non-volatile local
 variable is modified between the setjmp and the longjmp, it has
 indeterminate value.  For the purposes of LLVM IR, it may be
 sufficient to make loads and stores within the @try (to variables
 declared outside the @try) volatile.  This is necessary for
 optimized correctness, but is not currently being done; this is
 being tracked as rdar://problem/8160285
 
 The basic framework for a @try-catch-finally is as follows:
 {
 objc_exception_data d;
 id _rethrow = null;
 bool _call_try_exit = true;
 
 objc_exception_try_enter(&d);
 if (!setjmp(d.jmp_buf)) {
 ... try body ...
 } else {
 // exception path
 id _caught = objc_exception_extract(&d);
 
 // enter new try scope for handlers
 if (!setjmp(d.jmp_buf)) {
 ... match exception and execute catch blocks ...
 
 // fell off end, rethrow.
 _rethrow = _caught;
 ... jump-through-finally to finally_rethrow ...
 } else {
 // exception in catch block
 _rethrow = objc_exception_extract(&d);
 _call_try_exit = false;
 ... jump-through-finally to finally_rethrow ...
 }
 }
 ... jump-through-finally to finally_end ...
 
 finally:
 if (_call_try_exit)
 objc_exception_try_exit(&d);
 
 ... finally block ....
 ... dispatch to finally destination ...
 
 finally_rethrow:
 objc_exception_throw(_rethrow);
 
 finally_end:
 }
 
 This framework differs slightly from the one gcc uses, in that gcc
 uses _rethrow to determine if objc_exception_try_exit should be called
 and if the object should be rethrown. This breaks in the face of
 throwing nil and introduces unnecessary branches.
 
 We specialize this framework for a few particular circumstances:
 
 - If there are no catch blocks, then we avoid emitting the second
 exception handling context.
 
 - If there is a catch-all catch block (i.e. @catch(...) or @catch(id
 e)) we avoid emitting the code to rethrow an uncaught exception.
 
 - FIXME: If there is no @finally block we can do a few more
 simplifications.
 
 Rethrows and Jumps-Through-Finally
 --
 
 '@throw;' is supported by pushing the currently-caught exception
 onto ObjCEHStack while the @catch blocks are emitted.
 
 Branches through the @finally block are handled with an ordinary
 normal cleanup.  We do not register an EH cleanup; fragile-ABI ObjC
 exceptions are not compatible with C++ exceptions, and this is
 hardly the only place where this will go wrong.
 
 @synchronized(expr) { stmt; } is emitted as if it were:
 id synch_value = expr;
 objc_sync_enter(synch_value);
 @try { stmt; } @finally { objc_sync_exit(synch_value); }
 */

void CGObjCMulleRuntime::EmitTryOrSynchronizedStmt(CodeGen::CodeGenFunction &CGF,
                                          const Stmt &S) {
   bool isTry = isa<ObjCAtTryStmt>(S);
   
   // A destination for the fall-through edges of the catch handlers to
   // jump to.
   CodeGenFunction::JumpDest FinallyEnd =
   CGF.getJumpDestInCurrentScope("finally.end");
   
   // A destination for the rethrow edge of the catch handlers to jump
   // to.
   CodeGenFunction::JumpDest FinallyRethrow =
   CGF.getJumpDestInCurrentScope("finally.rethrow");
   
   // For @synchronized, call objc_sync_enter(sync.expr). The
   // evaluation of the expression must occur before we enter the
   // @synchronized.  We can't avoid a temp here because we need the
   // value to be preserved.  If the backend ever does liveness
   // correctly after setjmp, this will be unnecessary.
   llvm::Value *SyncArgSlot = nullptr;
   if (!isTry) {
      llvm::Value *SyncArg =
      CGF.EmitScalarExpr(cast<ObjCAtSynchronizedStmt>(S).getSynchExpr());
      SyncArg = CGF.Builder.CreateBitCast(SyncArg, ObjCTypes.ObjectPtrTy);
      CGF.EmitNounwindRuntimeCall(ObjCTypes.getSyncEnterFn(), SyncArg);
      
      SyncArgSlot = CGF.CreateTempAlloca(SyncArg->getType(), "sync.arg");
      CGF.Builder.CreateStore(SyncArg, SyncArgSlot);
   }
   
   // Allocate memory for the setjmp buffer.  This needs to be kept
   // live throughout the try and catch blocks.
   llvm::Value *ExceptionData = CGF.CreateTempAlloca(ObjCTypes.ExceptionDataTy,
                                                     "exceptiondata.ptr");
   
   // Create the fragile hazards.  Note that this will not capture any
   // of the allocas required for exception processing, but will
   // capture the current basic block (which extends all the way to the
   // setjmp call) as "before the @try".
   FragileHazards Hazards(CGF);
   
   // Create a flag indicating whether the cleanup needs to call
   // objc_exception_try_exit.  This is true except when
   //   - no catches match and we're branching through the cleanup
   //     just to rethrow the exception, or
   //   - a catch matched and we're falling out of the catch handler.
   // The setjmp-safety rule here is that we should always store to this
   // variable in a place that dominates the branch through the cleanup
   // without passing through any setjmps.
   llvm::Value *CallTryExitVar = CGF.CreateTempAlloca(CGF.Builder.getInt1Ty(),
                                                      "_call_try_exit");
   
   // A slot containing the exception to rethrow.  Only needed when we
   // have both a @catch and a @finally.
   llvm::Value *PropagatingExnVar = nullptr;
   
   // Push a normal cleanup to leave the try scope.
   CGF.EHStack.pushCleanup<PerformFragileFinally>(NormalAndEHCleanup, &S,
                                                  SyncArgSlot,
                                                  CallTryExitVar,
                                                  ExceptionData,
                                                  &ObjCTypes);
   
   // Enter a try block:
   //  - Call objc_exception_try_enter to push ExceptionData on top of
   //    the EH stack.
   CGF.EmitNounwindRuntimeCall(ObjCTypes.getExceptionTryEnterFn(), ExceptionData);
   
   //  - Call setjmp on the exception data buffer.
   llvm::Constant *Zero = llvm::ConstantInt::get(CGF.Builder.getInt32Ty(), 0);
   llvm::Value *GEPIndexes[] = { Zero, Zero, Zero };
   llvm::Value *SetJmpBuffer =
   CGF.Builder.CreateGEP(ExceptionData, GEPIndexes, "setjmp_buffer");
   llvm::CallInst *SetJmpResult =
   CGF.EmitNounwindRuntimeCall(ObjCTypes.getSetJmpFn(), SetJmpBuffer, "setjmp_result");
   SetJmpResult->setCanReturnTwice();
   
   // If setjmp returned 0, enter the protected block; otherwise,
   // branch to the handler.
   llvm::BasicBlock *TryBlock = CGF.createBasicBlock("try");
   llvm::BasicBlock *TryHandler = CGF.createBasicBlock("try.handler");
   llvm::Value *DidCatch =
   CGF.Builder.CreateIsNotNull(SetJmpResult, "did_catch_exception");
   CGF.Builder.CreateCondBr(DidCatch, TryHandler, TryBlock);
   
   // Emit the protected block.
   CGF.EmitBlock(TryBlock);
   CGF.Builder.CreateStore(CGF.Builder.getTrue(), CallTryExitVar);
   CGF.EmitStmt(isTry ? cast<ObjCAtTryStmt>(S).getTryBody()
                : cast<ObjCAtSynchronizedStmt>(S).getSynchBody());
   
   CGBuilderTy::InsertPoint TryFallthroughIP = CGF.Builder.saveAndClearIP();
   
   // Emit the exception handler block.
   CGF.EmitBlock(TryHandler);
   
   // Don't optimize loads of the in-scope locals across this point.
   Hazards.emitWriteHazard();
   
   // For a @synchronized (or a @try with no catches), just branch
   // through the cleanup to the rethrow block.
   if (!isTry || !cast<ObjCAtTryStmt>(S).getNumCatchStmts()) {
      // Tell the cleanup not to re-pop the exit.
      CGF.Builder.CreateStore(CGF.Builder.getFalse(), CallTryExitVar);
      CGF.EmitBranchThroughCleanup(FinallyRethrow);
      
      // Otherwise, we have to match against the caught exceptions.
   } else {
      // Retrieve the exception object.  We may emit multiple blocks but
      // nothing can cross this so the value is already in SSA form.
      llvm::CallInst *Caught =
      CGF.EmitNounwindRuntimeCall(ObjCTypes.getExceptionExtractFn(),
                                  ExceptionData, "caught");
      
      // Push the exception to rethrow onto the EH value stack for the
      // benefit of any @throws in the handlers.
      CGF.ObjCEHValueStack.push_back(Caught);
      
      const ObjCAtTryStmt* AtTryStmt = cast<ObjCAtTryStmt>(&S);
      
      bool HasFinally = (AtTryStmt->getFinallyStmt() != nullptr);
      
      llvm::BasicBlock *CatchBlock = nullptr;
      llvm::BasicBlock *CatchHandler = nullptr;
      if (HasFinally) {
         // Save the currently-propagating exception before
         // objc_exception_try_enter clears the exception slot.
         PropagatingExnVar = CGF.CreateTempAlloca(Caught->getType(),
                                                  "propagating_exception");
         CGF.Builder.CreateStore(Caught, PropagatingExnVar);
         
         // Enter a new exception try block (in case a @catch block
         // throws an exception).
         CGF.EmitNounwindRuntimeCall(ObjCTypes.getExceptionTryEnterFn(),
                                     ExceptionData);
         
         llvm::CallInst *SetJmpResult =
         CGF.EmitNounwindRuntimeCall(ObjCTypes.getSetJmpFn(),
                                     SetJmpBuffer, "setjmp.result");
         SetJmpResult->setCanReturnTwice();
         
         llvm::Value *Threw =
         CGF.Builder.CreateIsNotNull(SetJmpResult, "did_catch_exception");
         
         CatchBlock = CGF.createBasicBlock("catch");
         CatchHandler = CGF.createBasicBlock("catch_for_catch");
         CGF.Builder.CreateCondBr(Threw, CatchHandler, CatchBlock);
         
         CGF.EmitBlock(CatchBlock);
      }
      
      CGF.Builder.CreateStore(CGF.Builder.getInt1(HasFinally), CallTryExitVar);
      
      // Handle catch list. As a special case we check if everything is
      // matched and avoid generating code for falling off the end if
      // so.
      bool AllMatched = false;
      for (unsigned I = 0, N = AtTryStmt->getNumCatchStmts(); I != N; ++I) {
         const ObjCAtCatchStmt *CatchStmt = AtTryStmt->getCatchStmt(I);
         
         const VarDecl *CatchParam = CatchStmt->getCatchParamDecl();
         const ObjCObjectPointerType *OPT = nullptr;
         
         // catch(...) always matches.
         if (!CatchParam) {
            AllMatched = true;
         } else {
            OPT = CatchParam->getType()->getAs<ObjCObjectPointerType>();
            
            // catch(id e) always matches under this ABI, since only
            // ObjC exceptions end up here in the first place.
            // FIXME: For the time being we also match id<X>; this should
            // be rejected by Sema instead.
            if (OPT && (OPT->isObjCIdType() || OPT->isObjCQualifiedIdType()))
               AllMatched = true;
         }
         
         // If this is a catch-all, we don't need to test anything.
         if (AllMatched) {
            CodeGenFunction::RunCleanupsScope CatchVarCleanups(CGF);
            
            if (CatchParam) {
               CGF.EmitAutoVarDecl(*CatchParam);
               assert(CGF.HaveInsertPoint() && "DeclStmt destroyed insert point?");
               
               // These types work out because ConvertType(id) == i8*.
               CGF.Builder.CreateStore(Caught, CGF.GetAddrOfLocalVar(CatchParam));
            }
            
            CGF.EmitStmt(CatchStmt->getCatchBody());
            
            // The scope of the catch variable ends right here.
            CatchVarCleanups.ForceCleanup();
            
            CGF.EmitBranchThroughCleanup(FinallyEnd);
            break;
         }
         
         assert(OPT && "Unexpected non-object pointer type in @catch");
         const ObjCObjectType *ObjTy = OPT->getObjectType();
         
         // FIXME: @catch (Class c) ?
         ObjCInterfaceDecl *IDecl = ObjTy->getInterface();
         assert(IDecl && "Catch parameter must have Objective-C type!");
         
         // Check if the @catch block matches the exception object.
         llvm::Constant *Class = EmitClassRef(CGF, IDecl);
         
         llvm::Value *matchArgs[] = { Class, Caught };
         llvm::CallInst *Match =
         CGF.EmitNounwindRuntimeCall(ObjCTypes.getExceptionMatchFn(),
                                     matchArgs, "match");
         
         llvm::BasicBlock *MatchedBlock = CGF.createBasicBlock("match");
         llvm::BasicBlock *NextCatchBlock = CGF.createBasicBlock("catch.next");
         
         CGF.Builder.CreateCondBr(CGF.Builder.CreateIsNotNull(Match, "matched"),
                                  MatchedBlock, NextCatchBlock);
         
         // Emit the @catch block.
         CGF.EmitBlock(MatchedBlock);
         
         // Collect any cleanups for the catch variable.  The scope lasts until
         // the end of the catch body.
         CodeGenFunction::RunCleanupsScope CatchVarCleanups(CGF);
         
         CGF.EmitAutoVarDecl(*CatchParam);
         assert(CGF.HaveInsertPoint() && "DeclStmt destroyed insert point?");
         
         // Initialize the catch variable.
         llvm::Value *Tmp =
         CGF.Builder.CreateBitCast(Caught,
                                   CGF.ConvertType(CatchParam->getType()));
         CGF.Builder.CreateStore(Tmp, CGF.GetAddrOfLocalVar(CatchParam));
         
         CGF.EmitStmt(CatchStmt->getCatchBody());
         
         // We're done with the catch variable.
         CatchVarCleanups.ForceCleanup();
         
         CGF.EmitBranchThroughCleanup(FinallyEnd);
         
         CGF.EmitBlock(NextCatchBlock);
      }
      
      CGF.ObjCEHValueStack.pop_back();
      
      // If nothing wanted anything to do with the caught exception,
      // kill the extract call.
      if (Caught->use_empty())
         Caught->eraseFromParent();
      
      if (!AllMatched)
         CGF.EmitBranchThroughCleanup(FinallyRethrow);
      
      if (HasFinally) {
         // Emit the exception handler for the @catch blocks.
         CGF.EmitBlock(CatchHandler);
         
         // In theory we might now need a write hazard, but actually it's
         // unnecessary because there's no local-accessing code between
         // the try's write hazard and here.
         //Hazards.emitWriteHazard();
         
         // Extract the new exception and save it to the
         // propagating-exception slot.
         assert(PropagatingExnVar);
         llvm::CallInst *NewCaught =
         CGF.EmitNounwindRuntimeCall(ObjCTypes.getExceptionExtractFn(),
                                     ExceptionData, "caught");
         CGF.Builder.CreateStore(NewCaught, PropagatingExnVar);
         
         // Don't pop the catch handler; the throw already did.
         CGF.Builder.CreateStore(CGF.Builder.getFalse(), CallTryExitVar);
         CGF.EmitBranchThroughCleanup(FinallyRethrow);
      }
   }
   
   // Insert read hazards as required in the new blocks.
   Hazards.emitHazardsInNewBlocks();
   
   // Pop the cleanup.
   CGF.Builder.restoreIP(TryFallthroughIP);
   if (CGF.HaveInsertPoint())
      CGF.Builder.CreateStore(CGF.Builder.getTrue(), CallTryExitVar);
   CGF.PopCleanupBlock();
   CGF.EmitBlock(FinallyEnd.getBlock(), true);
   
   // Emit the rethrow block.
   CGBuilderTy::InsertPoint SavedIP = CGF.Builder.saveAndClearIP();
   CGF.EmitBlock(FinallyRethrow.getBlock(), true);
   if (CGF.HaveInsertPoint()) {
      // If we have a propagating-exception variable, check it.
      llvm::Value *PropagatingExn;
      if (PropagatingExnVar) {
         PropagatingExn = CGF.Builder.CreateLoad(PropagatingExnVar);
         
         // Otherwise, just look in the buffer for the exception to throw.
      } else {
         llvm::CallInst *Caught =
         CGF.EmitNounwindRuntimeCall(ObjCTypes.getExceptionExtractFn(),
                                     ExceptionData);
         PropagatingExn = Caught;
      }
      
      CGF.EmitNounwindRuntimeCall(ObjCTypes.getExceptionThrowFn(),
                                  PropagatingExn);
      CGF.Builder.CreateUnreachable();
   }
   
   CGF.Builder.restoreIP(SavedIP);
}

void CGObjCMulleRuntime::EmitThrowStmt(CodeGen::CodeGenFunction &CGF,
                              const ObjCAtThrowStmt &S,
                              bool ClearInsertionPoint) {
   llvm::Value *ExceptionAsObject;
   
   if (const Expr *ThrowExpr = S.getThrowExpr()) {
      llvm::Value *Exception = CGF.EmitObjCThrowOperand(ThrowExpr);
      ExceptionAsObject =
      CGF.Builder.CreateBitCast(Exception, ObjCTypes.ObjectPtrTy);
   } else {
      assert((!CGF.ObjCEHValueStack.empty() && CGF.ObjCEHValueStack.back()) &&
             "Unexpected rethrow outside @catch block.");
      ExceptionAsObject = CGF.ObjCEHValueStack.back();
   }
   
   CGF.EmitRuntimeCall(ObjCTypes.getExceptionThrowFn(), ExceptionAsObject)
   ->setDoesNotReturn();
   CGF.Builder.CreateUnreachable();
   
   // Clear the insertion point to indicate we are in unreachable code.
   if (ClearInsertionPoint)
      CGF.Builder.ClearInsertionPoint();
}

/// EmitObjCWeakRead - Code gen for loading value of a __weak
/// object: objc_read_weak (id *src)
///
llvm::Value * CGObjCMulleRuntime::EmitObjCWeakRead(CodeGen::CodeGenFunction &CGF,
                                          llvm::Value *AddrWeakObj) {
   llvm::Type* DestTy =
   cast<llvm::PointerType>(AddrWeakObj->getType())->getElementType();
   AddrWeakObj = CGF.Builder.CreateBitCast(AddrWeakObj,
                                           ObjCTypes.PtrObjectPtrTy);
   llvm::Value *read_weak =
   CGF.EmitNounwindRuntimeCall(ObjCTypes.getGcReadWeakFn(),
                               AddrWeakObj, "weakread");
   read_weak = CGF.Builder.CreateBitCast(read_weak, DestTy);
   return read_weak;
}

/// EmitObjCWeakAssign - Code gen for assigning to a __weak object.
/// objc_assign_weak (id src, id *dst)
///
void CGObjCMulleRuntime::EmitObjCWeakAssign(CodeGen::CodeGenFunction &CGF,
                                   llvm::Value *src, llvm::Value *dst) {
   llvm::Type * SrcTy = src->getType();
   if (!isa<llvm::PointerType>(SrcTy)) {
      unsigned Size = CGM.getDataLayout().getTypeAllocSize(SrcTy);
      assert(Size <= 8 && "does not support size > 8");
      src = (Size == 4) ? CGF.Builder.CreateBitCast(src, ObjCTypes.IntTy)
      : CGF.Builder.CreateBitCast(src, ObjCTypes.LongLongTy);
      src = CGF.Builder.CreateIntToPtr(src, ObjCTypes.Int8PtrTy);
   }
   src = CGF.Builder.CreateBitCast(src, ObjCTypes.ObjectPtrTy);
   dst = CGF.Builder.CreateBitCast(dst, ObjCTypes.PtrObjectPtrTy);
   llvm::Value *args[] = { src, dst };
   CGF.EmitNounwindRuntimeCall(ObjCTypes.getGcAssignWeakFn(),
                               args, "weakassign");
   return;
}

/// EmitObjCGlobalAssign - Code gen for assigning to a __strong object.
/// objc_assign_global (id src, id *dst)
///
void CGObjCMulleRuntime::EmitObjCGlobalAssign(CodeGen::CodeGenFunction &CGF,
                                     llvm::Value *src, llvm::Value *dst,
                                     bool threadlocal) {
   llvm::Type * SrcTy = src->getType();
   if (!isa<llvm::PointerType>(SrcTy)) {
      unsigned Size = CGM.getDataLayout().getTypeAllocSize(SrcTy);
      assert(Size <= 8 && "does not support size > 8");
      src = (Size == 4) ? CGF.Builder.CreateBitCast(src, ObjCTypes.IntTy)
      : CGF.Builder.CreateBitCast(src, ObjCTypes.LongLongTy);
      src = CGF.Builder.CreateIntToPtr(src, ObjCTypes.Int8PtrTy);
   }
   src = CGF.Builder.CreateBitCast(src, ObjCTypes.ObjectPtrTy);
   dst = CGF.Builder.CreateBitCast(dst, ObjCTypes.PtrObjectPtrTy);
   llvm::Value *args[] = { src, dst };
   if (!threadlocal)
      CGF.EmitNounwindRuntimeCall(ObjCTypes.getGcAssignGlobalFn(),
                                  args, "globalassign");
   else
      CGF.EmitNounwindRuntimeCall(ObjCTypes.getGcAssignThreadLocalFn(),
                                  args, "threadlocalassign");
   return;
}

/// EmitObjCIvarAssign - Code gen for assigning to a __strong object.
/// objc_assign_ivar (id src, id *dst, ptrdiff_t ivaroffset)
///
void CGObjCMulleRuntime::EmitObjCIvarAssign(CodeGen::CodeGenFunction &CGF,
                                   llvm::Value *src, llvm::Value *dst,
                                   llvm::Value *ivarOffset) {
   assert(ivarOffset && "EmitObjCIvarAssign - ivarOffset is NULL");
   llvm::Type * SrcTy = src->getType();
   if (!isa<llvm::PointerType>(SrcTy)) {
      unsigned Size = CGM.getDataLayout().getTypeAllocSize(SrcTy);
      assert(Size <= 8 && "does not support size > 8");
      src = (Size == 4) ? CGF.Builder.CreateBitCast(src, ObjCTypes.IntTy)
      : CGF.Builder.CreateBitCast(src, ObjCTypes.LongLongTy);
      src = CGF.Builder.CreateIntToPtr(src, ObjCTypes.Int8PtrTy);
   }
   src = CGF.Builder.CreateBitCast(src, ObjCTypes.ObjectPtrTy);
   dst = CGF.Builder.CreateBitCast(dst, ObjCTypes.PtrObjectPtrTy);
   llvm::Value *args[] = { src, dst, ivarOffset };
   CGF.EmitNounwindRuntimeCall(ObjCTypes.getGcAssignIvarFn(), args);
   return;
}

/// EmitObjCStrongCastAssign - Code gen for assigning to a __strong cast object.
/// objc_assign_strongCast (id src, id *dst)
///
void CGObjCMulleRuntime::EmitObjCStrongCastAssign(CodeGen::CodeGenFunction &CGF,
                                         llvm::Value *src, llvm::Value *dst) {
   llvm::Type * SrcTy = src->getType();
   if (!isa<llvm::PointerType>(SrcTy)) {
      unsigned Size = CGM.getDataLayout().getTypeAllocSize(SrcTy);
      assert(Size <= 8 && "does not support size > 8");
      src = (Size == 4) ? CGF.Builder.CreateBitCast(src, ObjCTypes.IntTy)
      : CGF.Builder.CreateBitCast(src, ObjCTypes.LongLongTy);
      src = CGF.Builder.CreateIntToPtr(src, ObjCTypes.Int8PtrTy);
   }
   src = CGF.Builder.CreateBitCast(src, ObjCTypes.ObjectPtrTy);
   dst = CGF.Builder.CreateBitCast(dst, ObjCTypes.PtrObjectPtrTy);
   llvm::Value *args[] = { src, dst };
   CGF.EmitNounwindRuntimeCall(ObjCTypes.getGcAssignStrongCastFn(),
                               args, "weakassign");
   return;
}

void CGObjCMulleRuntime::EmitGCMemmoveCollectable(CodeGen::CodeGenFunction &CGF,
                                         llvm::Value *DestPtr,
                                         llvm::Value *SrcPtr,
                                         llvm::Value *size) {
   SrcPtr = CGF.Builder.CreateBitCast(SrcPtr, ObjCTypes.Int8PtrTy);
   DestPtr = CGF.Builder.CreateBitCast(DestPtr, ObjCTypes.Int8PtrTy);
   llvm::Value *args[] = { DestPtr, SrcPtr, size };
   CGF.EmitNounwindRuntimeCall(ObjCTypes.GcMemmoveCollectableFn(), args);
}

/// EmitObjCValueForIvar - Code Gen for ivar reference.
///
LValue CGObjCMulleRuntime::EmitObjCValueForIvar(CodeGen::CodeGenFunction &CGF,
                                       QualType ObjectTy,
                                       llvm::Value *BaseValue,
                                       const ObjCIvarDecl *Ivar,
                                       unsigned CVRQualifiers) {
   const ObjCInterfaceDecl *ID =
   ObjectTy->getAs<ObjCObjectType>()->getInterface();
   return EmitValueForIvarAtOffset(CGF, ID, BaseValue, Ivar, CVRQualifiers,
                                   EmitIvarOffset(CGF, ID, Ivar));
}

llvm::Value *CGObjCMulleRuntime::EmitIvarOffset(CodeGen::CodeGenFunction &CGF,
                                       const ObjCInterfaceDecl *Interface,
                                       const ObjCIvarDecl *Ivar) {
   uint64_t Offset = ComputeIvarBaseOffset(CGM, Interface, Ivar);
   return llvm::ConstantInt::get(
                                 CGM.getTypes().ConvertType(CGM.getContext().LongTy),
                                 Offset);
}

/* *** Private Interface *** */

/// EmitImageInfo - Emit the image info marker used to encode some module
/// level information.
///
/// See: <rdr://4810609&4810587&4810587>
/// struct IMAGE_INFO {
///   unsigned version;
///   unsigned flags;
/// };
enum ImageInfoFlags {
   eImageInfo_FixAndContinue      = (1 << 0), // This flag is no longer set by clang.
   eImageInfo_GarbageCollected    = (1 << 1),
   eImageInfo_GCOnly              = (1 << 2),
   eImageInfo_OptimizedByDyld     = (1 << 3), // This flag is set by the dyld shared cache.
   
   // A flag indicating that the module has no instances of a @synthesize of a
   // superclass variable. <rdar://problem/6803242>
   eImageInfo_CorrectedSynthesize = (1 << 4), // This flag is no longer set by clang.
   eImageInfo_ImageIsSimulated    = (1 << 5)
};

void CGObjCCommonMulleRuntime::EmitImageInfo() {
   unsigned version = 0; // Version is unused?
   const char *Section = (ObjCABI == 1) ?
   "__DATA, __image_info,regular" :
   "__DATA, __objc_imageinfo, regular, no_dead_strip";
   
   // Generate module-level named metadata to convey this information to the
   // linker and code-gen.
   llvm::Module &Mod = CGM.getModule();
   
   // Add the ObjC ABI version to the module flags.
   Mod.addModuleFlag(llvm::Module::Error, "Objective-C Version", ObjCABI);
   Mod.addModuleFlag(llvm::Module::Error, "Objective-C Image Info Version",
                     version);
   Mod.addModuleFlag(llvm::Module::Error, "Objective-C Image Info Section",
                     llvm::MDString::get(VMContext,Section));
   
   if (CGM.getLangOpts().getGC() == LangOptions::NonGC) {
      // Non-GC overrides those files which specify GC.
      Mod.addModuleFlag(llvm::Module::Override,
                        "Objective-C Garbage Collection", (uint32_t)0);
   } else {
      // Add the ObjC garbage collection value.
      Mod.addModuleFlag(llvm::Module::Error,
                        "Objective-C Garbage Collection",
                        eImageInfo_GarbageCollected);
      
      if (CGM.getLangOpts().getGC() == LangOptions::GCOnly) {
         // Add the ObjC GC Only value.
         Mod.addModuleFlag(llvm::Module::Error, "Objective-C GC Only",
                           eImageInfo_GCOnly);
         
         // Require that GC be specified and set to eImageInfo_GarbageCollected.
         llvm::Metadata *Ops[2] = {
            llvm::MDString::get(VMContext, "Objective-C Garbage Collection"),
            llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(
                                                                 llvm::Type::getInt32Ty(VMContext), eImageInfo_GarbageCollected))};
         Mod.addModuleFlag(llvm::Module::Require, "Objective-C GC Only",
                           llvm::MDNode::get(VMContext, Ops));
      }
   }
   
   // Indicate whether we're compiling this to run on a simulator.
   const llvm::Triple &Triple = CGM.getTarget().getTriple();
   if (Triple.isiOS() &&
       (Triple.getArch() == llvm::Triple::x86 ||
        Triple.getArch() == llvm::Triple::x86_64))
      Mod.addModuleFlag(llvm::Module::Error, "Objective-C Is Simulated",
                        eImageInfo_ImageIsSimulated);
}

// struct objc_module {
//   unsigned long version;
//   unsigned long size;
//   const char *name;
//   Symtab symtab;
// };

// FIXME: Get from somewhere
static const int ModuleVersion = 7;

void CGObjCMulleRuntime::EmitModuleInfo() {
   uint64_t Size = CGM.getDataLayout().getTypeAllocSize(ObjCTypes.ModuleTy);
   
   llvm::Constant *Values[] = {
      llvm::ConstantInt::get(ObjCTypes.LongTy, ModuleVersion),
      llvm::ConstantInt::get(ObjCTypes.LongTy, Size),
      // This used to be the filename, now it is unused. <rdr://4327263>
      GetClassName(StringRef("")),
      EmitModuleSymbols()
   };
   CreateMetadataVar("OBJC_MODULES",
                     llvm::ConstantStruct::get(ObjCTypes.ModuleTy, Values),
                     "__DATA,__module_info,regular,no_dead_strip", 4, true);
}

llvm::Constant *CGObjCMulleRuntime::EmitModuleSymbols() {
   unsigned NumClasses = DefinedClasses.size();
   unsigned NumCategories = DefinedCategories.size();
   
   // Return null if no symbols were defined.
   if (!NumClasses && !NumCategories)
      return llvm::Constant::getNullValue(ObjCTypes.SymtabPtrTy);
   
   llvm::Constant *Values[5];
   Values[0] = llvm::ConstantInt::get(ObjCTypes.LongTy, 0);
   Values[1] = llvm::Constant::getNullValue(ObjCTypes.SelectorIDTy);
   Values[2] = llvm::ConstantInt::get(ObjCTypes.ShortTy, NumClasses);
   Values[3] = llvm::ConstantInt::get(ObjCTypes.ShortTy, NumCategories);
   
   // The runtime expects exactly the list of defined classes followed
   // by the list of defined categories, in a single array.
   SmallVector<llvm::Constant*, 8> Symbols(NumClasses + NumCategories);
   for (unsigned i=0; i<NumClasses; i++) {
      const ObjCInterfaceDecl *ID = ImplementedClasses[i];
      assert(ID);
      if (ObjCImplementationDecl *IMP = ID->getImplementation())
         // We are implementing a weak imported interface. Give it external linkage
         if (ID->isWeakImported() && !IMP->isWeakImported())
            DefinedClasses[i]->setLinkage(llvm::GlobalVariable::ExternalLinkage);
      
      Symbols[i] = llvm::ConstantExpr::getBitCast(DefinedClasses[i],
                                                  ObjCTypes.Int8PtrTy);
   }
   for (unsigned i=0; i<NumCategories; i++)
      Symbols[NumClasses + i] =
      llvm::ConstantExpr::getBitCast(DefinedCategories[i],
                                     ObjCTypes.Int8PtrTy);
   
   Values[4] =
   llvm::ConstantArray::get(llvm::ArrayType::get(ObjCTypes.Int8PtrTy,
                                                 Symbols.size()),
                            Symbols);
   
   llvm::Constant *Init = llvm::ConstantStruct::getAnon(Values);
   
   llvm::GlobalVariable *GV = CreateMetadataVar(
                                                "OBJC_SYMBOLS", Init, "__DATA,__symbols,regular,no_dead_strip", 4, true);
   return llvm::ConstantExpr::getBitCast(GV, ObjCTypes.SymtabPtrTy);
}

llvm::Constant *CGObjCMulleRuntime::EmitClassRefFromId(CodeGenFunction &CGF,
                                           IdentifierInfo *II) {
   LazySymbols.insert(II);
   
   return( HashConstantForString( II->getName()));
}

llvm::Constant *CGObjCMulleRuntime::EmitClassRef(CodeGenFunction &CGF,
                                     const ObjCInterfaceDecl *ID) {
   return EmitClassRefFromId(CGF, ID->getIdentifier());
}

llvm::Constant *CGObjCMulleRuntime::EmitNSAutoreleasePoolClassRef(CodeGenFunction &CGF) {
   IdentifierInfo *II = &CGM.getContext().Idents.get("NSAutoreleasePool");
   return EmitClassRefFromId(CGF, II);
}


llvm::Constant *CGObjCCommonMulleRuntime::HashConstantForString( StringRef sref)
{
   unsigned WordSizeInBits = CGM.getTarget().getPointerWidth(0);
   unsigned ByteSizeInBits = CGM.getTarget().getCharWidth();
   unsigned WordSizeInBytes = WordSizeInBits/ByteSizeInBits;
   
   std::string           name;
   llvm::MD5             MD5Ctx;
   llvm::MD5::MD5Result  hash;
   uint64_t              value;
   unsigned int          i;
   
   MD5Ctx.update( sref);
   MD5Ctx.final( hash);
   
   value = 0;
   for( i = 0; i < WordSizeInBytes; i++)
   {
      value <<= 8;
      value  |= hash[ i];
   }
   
   if (WordSizeInBytes == 8)
   {
      const llvm::APInt SelConstant(64, value);
      return llvm::Constant::getIntegerValue(CGM.Int64Ty, SelConstant);
   }
   else
   {
      const llvm::APInt SelConstant(32, value);
      return llvm::Constant::getIntegerValue(CGM.Int32Ty, SelConstant);
   }
}


llvm::Constant *CGObjCMulleRuntime::EmitSelector(CodeGenFunction &CGF, Selector Sel,
                                     bool lvalue)
{
   llvm::StringRef   sref( Sel.getAsString());
   
   return( HashConstantForString( sref));
}


llvm::Constant *CGObjCCommonMulleRuntime::GetClassName(StringRef RuntimeName) {
    llvm::GlobalVariable *&Entry = ClassNames[RuntimeName];
    if (!Entry)
      Entry = CreateMetadataVar(
          "OBJC_CLASS_NAME_",
          llvm::ConstantDataArray::getString(VMContext, RuntimeName),
          ((ObjCABI == 2) ? "__TEXT,__objc_classname,cstring_literals"
                          : "__TEXT,__cstring,cstring_literals"),
          1, true);
    return getConstantGEP(VMContext, Entry, 0, 0);
}

llvm::Function *CGObjCCommonMulleRuntime::GetMethodDefinition(const ObjCMethodDecl *MD) {
   llvm::DenseMap<const ObjCMethodDecl*, llvm::Function*>::iterator
   I = MethodDefinitions.find(MD);
   if (I != MethodDefinitions.end())
      return I->second;
   
   return nullptr;
}

# pragma mark -
# pragma mark Ivar code stolen from Mac runtime

/// GetIvarLayoutName - Returns a unique constant for the given
/// ivar layout bitmap.
llvm::Constant *CGObjCCommonMulleRuntime::GetIvarLayoutName(IdentifierInfo *Ident,
                                                   const ObjCCommonTypesHelper &ObjCTypes) {
   return llvm::Constant::getNullValue(ObjCTypes.Int8PtrTy);
}

void CGObjCCommonMulleRuntime::BuildAggrIvarRecordLayout(const RecordType *RT,
                                                unsigned int BytePos,
                                                bool ForStrongLayout,
                                                bool &HasUnion) {
   const RecordDecl *RD = RT->getDecl();
   // FIXME - Use iterator.
   SmallVector<const FieldDecl*, 16> Fields(RD->fields());
   llvm::Type *Ty = CGM.getTypes().ConvertType(QualType(RT, 0));
   const llvm::StructLayout *RecLayout =
   CGM.getDataLayout().getStructLayout(cast<llvm::StructType>(Ty));
   
   BuildAggrIvarLayout(nullptr, RecLayout, RD, Fields, BytePos, ForStrongLayout,
                       HasUnion);
}

void CGObjCCommonMulleRuntime::BuildAggrIvarLayout(const ObjCImplementationDecl *OI,
                                          const llvm::StructLayout *Layout,
                                          const RecordDecl *RD,
                                          ArrayRef<const FieldDecl*> RecFields,
                                          unsigned int BytePos, bool ForStrongLayout,
                                          bool &HasUnion) {
   bool IsUnion = (RD && RD->isUnion());
   uint64_t MaxUnionIvarSize = 0;
   uint64_t MaxSkippedUnionIvarSize = 0;
   const FieldDecl *MaxField = nullptr;
   const FieldDecl *MaxSkippedField = nullptr;
   const FieldDecl *LastFieldBitfieldOrUnnamed = nullptr;
   uint64_t MaxFieldOffset = 0;
   uint64_t MaxSkippedFieldOffset = 0;
   uint64_t LastBitfieldOrUnnamedOffset = 0;
   uint64_t FirstFieldDelta = 0;
   
   if (RecFields.empty())
      return;
   unsigned WordSizeInBits = CGM.getTarget().getPointerWidth(0);
   unsigned ByteSizeInBits = CGM.getTarget().getCharWidth();
   if (!RD && CGM.getLangOpts().ObjCAutoRefCount) {
      const FieldDecl *FirstField = RecFields[0];
      FirstFieldDelta =
      ComputeIvarBaseOffset(CGM, OI, cast<ObjCIvarDecl>(FirstField));
   }
   
   for (unsigned i = 0, e = RecFields.size(); i != e; ++i) {
      const FieldDecl *Field = RecFields[i];
      uint64_t FieldOffset;
      if (RD) {
         // Note that 'i' here is actually the field index inside RD of Field,
         // although this dependency is hidden.
         const ASTRecordLayout &RL = CGM.getContext().getASTRecordLayout(RD);
         FieldOffset = (RL.getFieldOffset(i) / ByteSizeInBits) - FirstFieldDelta;
      } else
         FieldOffset =
         ComputeIvarBaseOffset(CGM, OI, cast<ObjCIvarDecl>(Field)) - FirstFieldDelta;
      
      // Skip over unnamed or bitfields
      if (!Field->getIdentifier() || Field->isBitField()) {
         LastFieldBitfieldOrUnnamed = Field;
         LastBitfieldOrUnnamedOffset = FieldOffset;
         continue;
      }
      
      LastFieldBitfieldOrUnnamed = nullptr;
      QualType FQT = Field->getType();
      if (FQT->isRecordType() || FQT->isUnionType()) {
         if (FQT->isUnionType())
            HasUnion = true;
         
         BuildAggrIvarRecordLayout(FQT->getAs<RecordType>(),
                                   BytePos + FieldOffset,
                                   ForStrongLayout, HasUnion);
         continue;
      }
      
      if (const ArrayType *Array = CGM.getContext().getAsArrayType(FQT)) {
         const ConstantArrayType *CArray =
         dyn_cast_or_null<ConstantArrayType>(Array);
         uint64_t ElCount = CArray->getSize().getZExtValue();
         assert(CArray && "only array with known element size is supported");
         FQT = CArray->getElementType();
         while (const ArrayType *Array = CGM.getContext().getAsArrayType(FQT)) {
            const ConstantArrayType *CArray =
            dyn_cast_or_null<ConstantArrayType>(Array);
            ElCount *= CArray->getSize().getZExtValue();
            FQT = CArray->getElementType();
         }
         if (FQT->isRecordType() && ElCount) {
            int OldIndex = IvarsInfo.size() - 1;
            int OldSkIndex = SkipIvars.size() -1;
            
            const RecordType *RT = FQT->getAs<RecordType>();
            BuildAggrIvarRecordLayout(RT, BytePos + FieldOffset,
                                      ForStrongLayout, HasUnion);
            
            // Replicate layout information for each array element. Note that
            // one element is already done.
            uint64_t ElIx = 1;
            for (int FirstIndex = IvarsInfo.size() - 1,
                 FirstSkIndex = SkipIvars.size() - 1 ;ElIx < ElCount; ElIx++) {
               uint64_t Size = CGM.getContext().getTypeSize(RT)/ByteSizeInBits;
               for (int i = OldIndex+1; i <= FirstIndex; ++i)
                  IvarsInfo.push_back(GC_IVAR(IvarsInfo[i].ivar_bytepos + Size*ElIx,
                                              IvarsInfo[i].ivar_size));
               for (int i = OldSkIndex+1; i <= FirstSkIndex; ++i)
                  SkipIvars.push_back(GC_IVAR(SkipIvars[i].ivar_bytepos + Size*ElIx,
                                              SkipIvars[i].ivar_size));
            }
            continue;
         }
      }
      // At this point, we are done with Record/Union and array there of.
      // For other arrays we are down to its element type.
      Qualifiers::GC GCAttr = GetGCAttrTypeForType(CGM.getContext(), FQT);
      
      unsigned FieldSize = CGM.getContext().getTypeSize(Field->getType());
      if ((ForStrongLayout && GCAttr == Qualifiers::Strong)
          || (!ForStrongLayout && GCAttr == Qualifiers::Weak)) {
         if (IsUnion) {
            uint64_t UnionIvarSize = FieldSize / WordSizeInBits;
            if (UnionIvarSize > MaxUnionIvarSize) {
               MaxUnionIvarSize = UnionIvarSize;
               MaxField = Field;
               MaxFieldOffset = FieldOffset;
            }
         } else {
            IvarsInfo.push_back(GC_IVAR(BytePos + FieldOffset,
                                        FieldSize / WordSizeInBits));
         }
      } else if ((ForStrongLayout &&
                  (GCAttr == Qualifiers::GCNone || GCAttr == Qualifiers::Weak))
                 || (!ForStrongLayout && GCAttr != Qualifiers::Weak)) {
         if (IsUnion) {
            // FIXME: Why the asymmetry? We divide by word size in bits on other
            // side.
            uint64_t UnionIvarSize = FieldSize / ByteSizeInBits;
            if (UnionIvarSize > MaxSkippedUnionIvarSize) {
               MaxSkippedUnionIvarSize = UnionIvarSize;
               MaxSkippedField = Field;
               MaxSkippedFieldOffset = FieldOffset;
            }
         } else {
            // FIXME: Why the asymmetry, we divide by byte size in bits here?
            SkipIvars.push_back(GC_IVAR(BytePos + FieldOffset,
                                        FieldSize / ByteSizeInBits));
         }
      }
   }
   
   if (LastFieldBitfieldOrUnnamed) {
      if (LastFieldBitfieldOrUnnamed->isBitField()) {
         // Last field was a bitfield. Must update skip info.
         uint64_t BitFieldSize
         = LastFieldBitfieldOrUnnamed->getBitWidthValue(CGM.getContext());
         GC_IVAR skivar;
         skivar.ivar_bytepos = BytePos + LastBitfieldOrUnnamedOffset;
         skivar.ivar_size = (BitFieldSize / ByteSizeInBits)
         + ((BitFieldSize % ByteSizeInBits) != 0);
         SkipIvars.push_back(skivar);
      } else {
         assert(!LastFieldBitfieldOrUnnamed->getIdentifier() &&"Expected unnamed");
         // Last field was unnamed. Must update skip info.
         unsigned FieldSize
         = CGM.getContext().getTypeSize(LastFieldBitfieldOrUnnamed->getType());
         SkipIvars.push_back(GC_IVAR(BytePos + LastBitfieldOrUnnamedOffset,
                                     FieldSize / ByteSizeInBits));
      }
   }
   
   if (MaxField)
      IvarsInfo.push_back(GC_IVAR(BytePos + MaxFieldOffset,
                                  MaxUnionIvarSize));
   if (MaxSkippedField)
      SkipIvars.push_back(GC_IVAR(BytePos + MaxSkippedFieldOffset,
                                  MaxSkippedUnionIvarSize));
}

/// BuildIvarLayoutBitmap - This routine is the horsework for doing all
/// the computations and returning the layout bitmap (for ivar or blocks) in
/// the given argument BitMap string container. Routine reads
/// two containers, IvarsInfo and SkipIvars which are assumed to be
/// filled already by the caller.
llvm::Constant *CGObjCCommonMulleRuntime::BuildIvarLayoutBitmap(std::string &BitMap) {
   unsigned int WordsToScan, WordsToSkip;
   llvm::Type *PtrTy = CGM.Int8PtrTy;
   
   // Build the string of skip/scan nibbles
   SmallVector<SKIP_SCAN, 32> SkipScanIvars;
   unsigned int WordSize =
   CGM.getTypes().getDataLayout().getTypeAllocSize(PtrTy);
   if (IvarsInfo[0].ivar_bytepos == 0) {
      WordsToSkip = 0;
      WordsToScan = IvarsInfo[0].ivar_size;
   } else {
      WordsToSkip = IvarsInfo[0].ivar_bytepos/WordSize;
      WordsToScan = IvarsInfo[0].ivar_size;
   }
   for (unsigned int i=1, Last=IvarsInfo.size(); i != Last; i++) {
      unsigned int TailPrevGCObjC =
      IvarsInfo[i-1].ivar_bytepos + IvarsInfo[i-1].ivar_size * WordSize;
      if (IvarsInfo[i].ivar_bytepos == TailPrevGCObjC) {
         // consecutive 'scanned' object pointers.
         WordsToScan += IvarsInfo[i].ivar_size;
      } else {
         // Skip over 'gc'able object pointer which lay over each other.
         if (TailPrevGCObjC > IvarsInfo[i].ivar_bytepos)
            continue;
         // Must skip over 1 or more words. We save current skip/scan values
         //  and start a new pair.
         SKIP_SCAN SkScan;
         SkScan.skip = WordsToSkip;
         SkScan.scan = WordsToScan;
         SkipScanIvars.push_back(SkScan);
         
         // Skip the hole.
         SkScan.skip = (IvarsInfo[i].ivar_bytepos - TailPrevGCObjC) / WordSize;
         SkScan.scan = 0;
         SkipScanIvars.push_back(SkScan);
         WordsToSkip = 0;
         WordsToScan = IvarsInfo[i].ivar_size;
      }
   }
   if (WordsToScan > 0) {
      SKIP_SCAN SkScan;
      SkScan.skip = WordsToSkip;
      SkScan.scan = WordsToScan;
      SkipScanIvars.push_back(SkScan);
   }
   
   if (!SkipIvars.empty()) {
      unsigned int LastIndex = SkipIvars.size()-1;
      int LastByteSkipped =
      SkipIvars[LastIndex].ivar_bytepos + SkipIvars[LastIndex].ivar_size;
      LastIndex = IvarsInfo.size()-1;
      int LastByteScanned =
      IvarsInfo[LastIndex].ivar_bytepos +
      IvarsInfo[LastIndex].ivar_size * WordSize;
      // Compute number of bytes to skip at the tail end of the last ivar scanned.
      if (LastByteSkipped > LastByteScanned) {
         unsigned int TotalWords = (LastByteSkipped + (WordSize -1)) / WordSize;
         SKIP_SCAN SkScan;
         SkScan.skip = TotalWords - (LastByteScanned/WordSize);
         SkScan.scan = 0;
         SkipScanIvars.push_back(SkScan);
      }
   }
   // Mini optimization of nibbles such that an 0xM0 followed by 0x0N is produced
   // as 0xMN.
   int SkipScan = SkipScanIvars.size()-1;
   for (int i = 0; i <= SkipScan; i++) {
      if ((i < SkipScan) && SkipScanIvars[i].skip && SkipScanIvars[i].scan == 0
          && SkipScanIvars[i+1].skip == 0 && SkipScanIvars[i+1].scan) {
         // 0xM0 followed by 0x0N detected.
         SkipScanIvars[i].scan = SkipScanIvars[i+1].scan;
         for (int j = i+1; j < SkipScan; j++)
            SkipScanIvars[j] = SkipScanIvars[j+1];
         --SkipScan;
      }
   }
   
   // Generate the string.
   for (int i = 0; i <= SkipScan; i++) {
      unsigned char byte;
      unsigned int skip_small = SkipScanIvars[i].skip % 0xf;
      unsigned int scan_small = SkipScanIvars[i].scan % 0xf;
      unsigned int skip_big  = SkipScanIvars[i].skip / 0xf;
      unsigned int scan_big  = SkipScanIvars[i].scan / 0xf;
      
      // first skip big.
      for (unsigned int ix = 0; ix < skip_big; ix++)
         BitMap += (unsigned char)(0xf0);
      
      // next (skip small, scan)
      if (skip_small) {
         byte = skip_small << 4;
         if (scan_big > 0) {
            byte |= 0xf;
            --scan_big;
         } else if (scan_small) {
            byte |= scan_small;
            scan_small = 0;
         }
         BitMap += byte;
      }
      // next scan big
      for (unsigned int ix = 0; ix < scan_big; ix++)
         BitMap += (unsigned char)(0x0f);
      // last scan small
      if (scan_small) {
         byte = scan_small;
         BitMap += byte;
      }
   }
   // null terminate string.
   unsigned char zero = 0;
   BitMap += zero;
   
   llvm::GlobalVariable *Entry = CreateMetadataVar(
                                                   "OBJC_CLASS_NAME_",
                                                   llvm::ConstantDataArray::getString(VMContext, BitMap, false),
                                                   ((ObjCABI == 2) ? "__TEXT,__objc_classname,cstring_literals"
                                                    : "__TEXT,__cstring,cstring_literals"),
                                                   1, true);
   return getConstantGEP(VMContext, Entry, 0, 0);
}

/// BuildIvarLayout - Builds ivar layout bitmap for the class
/// implementation for the __strong or __weak case.
/// The layout map displays which words in ivar list must be skipped
/// and which must be scanned by GC (see below). String is built of bytes.
/// Each byte is divided up in two nibbles (4-bit each). Left nibble is count
/// of words to skip and right nibble is count of words to scan. So, each
/// nibble represents up to 15 workds to skip or scan. Skipping the rest is
/// represented by a 0x00 byte which also ends the string.
/// 1. when ForStrongLayout is true, following ivars are scanned:
/// - id, Class
/// - object *
/// - __strong anything
///
/// 2. When ForStrongLayout is false, following ivars are scanned:
/// - __weak anything
///
llvm::Constant *CGObjCCommonMulleRuntime::BuildIvarLayout(
                                                 const ObjCImplementationDecl *OMD,
                                                 bool ForStrongLayout) {
   bool hasUnion = false;
   
   llvm::Type *PtrTy = CGM.Int8PtrTy;
   if (CGM.getLangOpts().getGC() == LangOptions::NonGC &&
       !CGM.getLangOpts().ObjCAutoRefCount)
      return llvm::Constant::getNullValue(PtrTy);
   
   const ObjCInterfaceDecl *OI = OMD->getClassInterface();
   SmallVector<const FieldDecl*, 32> RecFields;
   if (CGM.getLangOpts().ObjCAutoRefCount) {
      for (const ObjCIvarDecl *IVD = OI->all_declared_ivar_begin();
           IVD; IVD = IVD->getNextIvar())
         RecFields.push_back(cast<FieldDecl>(IVD));
   }
   else {
      SmallVector<const ObjCIvarDecl*, 32> Ivars;
      CGM.getContext().DeepCollectObjCIvars(OI, true, Ivars);
      
      // FIXME: This is not ideal; we shouldn't have to do this copy.
      RecFields.append(Ivars.begin(), Ivars.end());
   }
   
   if (RecFields.empty())
      return llvm::Constant::getNullValue(PtrTy);
   
   SkipIvars.clear();
   IvarsInfo.clear();
   
   BuildAggrIvarLayout(OMD, nullptr, nullptr, RecFields, 0, ForStrongLayout,
                       hasUnion);
   if (IvarsInfo.empty())
      return llvm::Constant::getNullValue(PtrTy);
   // Sort on byte position in case we encounterred a union nested in
   // the ivar list.
   if (hasUnion && !IvarsInfo.empty())
      std::sort(IvarsInfo.begin(), IvarsInfo.end());
   if (hasUnion && !SkipIvars.empty())
      std::sort(SkipIvars.begin(), SkipIvars.end());
   
   std::string BitMap;
   llvm::Constant *C = BuildIvarLayoutBitmap(BitMap);
   
   if (CGM.getLangOpts().ObjCGCBitmapPrint) {
      printf("\n%s ivar layout for class '%s': ",
             ForStrongLayout ? "strong" : "weak",
             OMD->getClassInterface()->getName().str().c_str());
      const unsigned char *s = (const unsigned char*)BitMap.c_str();
      for (unsigned i = 0, e = BitMap.size(); i < e; i++)
         if (!(s[i] & 0xf0))
            printf("0x0%x%s", s[i], s[i] != 0 ? ", " : "");
         else
            printf("0x%x%s",  s[i], s[i] != 0 ? ", " : "");
      printf("\n");
   }
   return C;
}

# pragma mark -
# pragma mark Aux code

llvm::Constant *CGObjCCommonMulleRuntime::GetMethodVarName(Selector Sel)
{
   llvm::GlobalVariable *&Entry = MethodVarNames[Sel];
   
   // FIXME: Avoid std::string in "Sel.getAsString()"
   if (!Entry)
      Entry = CreateMetadataVar(
                                "OBJC_METH_VAR_NAME_",
                                llvm::ConstantDataArray::getString(VMContext, Sel.getAsString()),
                                ((ObjCABI == 2) ? "__TEXT,__objc_methname,cstring_literals"
                                 : "__TEXT,__cstring,cstring_literals"),
                                1, true);
   
   return getConstantGEP(VMContext, Entry, 0, 0);
}

// FIXME: Merge into a single cstring creation function.
llvm::Constant *CGObjCCommonMulleRuntime::GetMethodVarName(IdentifierInfo *ID) {
   return GetMethodVarName(CGM.getContext().Selectors.getNullarySelector(ID));
}

llvm::Constant *CGObjCCommonMulleRuntime::GetMethodVarType(const FieldDecl *Field) {
   std::string TypeStr;
   CGM.getContext().getObjCEncodingForType(Field->getType(), TypeStr, Field);
   
   llvm::GlobalVariable *&Entry = MethodVarTypes[TypeStr];
   
   if (!Entry)
      Entry = CreateMetadataVar(
                                "OBJC_METH_VAR_TYPE_",
                                llvm::ConstantDataArray::getString(VMContext, TypeStr),
                                ((ObjCABI == 2) ? "__TEXT,__objc_methtype,cstring_literals"
                                 : "__TEXT,__cstring,cstring_literals"),
                                1, true);
   
   return getConstantGEP(VMContext, Entry, 0, 0);
}

llvm::Constant *CGObjCCommonMulleRuntime::GetMethodVarType(const ObjCMethodDecl *D,
                                                  bool Extended) {
   std::string TypeStr;
   if (CGM.getContext().getObjCEncodingForMethodDecl(D, TypeStr, Extended))
      return nullptr;
   
   llvm::GlobalVariable *&Entry = MethodVarTypes[TypeStr];
   
   if (!Entry)
      Entry = CreateMetadataVar(
                                "OBJC_METH_VAR_TYPE_",
                                llvm::ConstantDataArray::getString(VMContext, TypeStr),
                                ((ObjCABI == 2) ? "__TEXT,__objc_methtype,cstring_literals"
                                 : "__TEXT,__cstring,cstring_literals"),
                                1, true);
   
   return getConstantGEP(VMContext, Entry, 0, 0);
}

// FIXME: Merge into a single cstring creation function.
llvm::Constant *CGObjCCommonMulleRuntime::GetPropertyName(IdentifierInfo *Ident) {
   llvm::GlobalVariable *&Entry = PropertyNames[Ident];
   
   if (!Entry)
      Entry = CreateMetadataVar(
                                "OBJC_PROP_NAME_ATTR_",
                                llvm::ConstantDataArray::getString(VMContext, Ident->getName()),
                                "__TEXT,__cstring,cstring_literals", 1, true);
   
   return getConstantGEP(VMContext, Entry, 0, 0);
}

// FIXME: Merge into a single cstring creation function.
// FIXME: This Decl should be more precise.
llvm::Constant *
CGObjCCommonMulleRuntime::GetPropertyTypeString(const ObjCPropertyDecl *PD,
                                       const Decl *Container) {
   std::string TypeStr;
   CGM.getContext().getObjCEncodingForPropertyDecl(PD, Container, TypeStr);
   return GetPropertyName(&CGM.getContext().Idents.get(TypeStr));
}

void CGObjCCommonMulleRuntime::GetNameForMethod(const ObjCMethodDecl *D,
                                       const ObjCContainerDecl *CD,
                                       SmallVectorImpl<char> &Name) {
   llvm::raw_svector_ostream OS(Name);
   assert (CD && "Missing container decl in GetNameForMethod");
   OS << '\01' << (D->isInstanceMethod() ? '-' : '+')
   << '[' << CD->getName();
   if (const ObjCCategoryImplDecl *CID =
       dyn_cast<ObjCCategoryImplDecl>(D->getDeclContext()))
      OS << '(' << *CID << ')';
   OS << ' ' << D->getSelector().getAsString() << ']';
}

void CGObjCMulleRuntime::FinishModule() {

   // take collected initializers and create a __attribute__(constructor)
   // static void   __load() function
   // that does the appropriate calls to setup the runtime
   
}


/* *** */

ObjCCommonTypesHelper::ObjCCommonTypesHelper(CodeGen::CodeGenModule &cgm)
: VMContext(cgm.getLLVMContext()), CGM(cgm), ExternalProtocolPtrTy(nullptr)
{
   CodeGen::CodeGenTypes &Types = CGM.getTypes();
   ASTContext &Ctx = CGM.getContext();
   
   ShortTy = Types.ConvertType(Ctx.ShortTy);
   IntTy = Types.ConvertType(Ctx.IntTy);
   LongTy = Types.ConvertType(Ctx.LongTy);
   LongLongTy = Types.ConvertType(Ctx.LongLongTy);
   Int8PtrTy = CGM.Int8PtrTy;
   Int8PtrPtrTy = CGM.Int8PtrPtrTy;

   ClassIDTy    = Types.ConvertType(Ctx.LongTy);
   CategoryIDTy = Types.ConvertType(Ctx.LongTy);
   SelectorIDTy = Types.ConvertType(Ctx.LongTy);
   ProtocolIDTy = Types.ConvertType(Ctx.LongTy);
   
   ClassIDPtrTy      = llvm::PointerType::getUnqual(ClassIDTy);
   CategoryIDTyPtrTy = llvm::PointerType::getUnqual(CategoryIDTy);
   SelectorIDTyPtrTy = llvm::PointerType::getUnqual(SelectorIDTy);
   ProtocolIDPtrTy   = llvm::PointerType::getUnqual(ProtocolIDTy);

   // arm64 targets use "int" ivar offset variables. All others,
   // including OS X x86_64 and Windows x86_64, use "long" ivar offsets.
   if (CGM.getTarget().getTriple().getArch() == llvm::Triple::aarch64)
      IvarOffsetVarTy = IntTy;
   else
      IvarOffsetVarTy = LongTy;
   
   ObjectPtrTy    = Types.ConvertType(Ctx.getObjCIdType());
   PtrObjectPtrTy = llvm::PointerType::getUnqual(ObjectPtrTy);
   ParamsPtrTy    = llvm::PointerType::getUnqual(Types.ConvertType(Ctx.VoidTy));
   
   // I'm not sure I like this. The implicit coordination is a bit
   // gross. We should solve this in a reasonable fashion because this
   // is a pretty common task (match some runtime data structure with
   // an LLVM data structure).
   
   // FIXME: This is leaked.
   // FIXME: Merge with rewriter code?
   
   // struct _objc_super {
   //   id self;
   //   Class cls;
   // }
   RecordDecl *RD = RecordDecl::Create(Ctx, TTK_Struct,
                                       Ctx.getTranslationUnitDecl(),
                                       SourceLocation(), SourceLocation(),
                                       &Ctx.Idents.get("_objc_super"));
   RD->addDecl(FieldDecl::Create(Ctx, RD, SourceLocation(), SourceLocation(),
                                 nullptr, Ctx.getObjCIdType(), nullptr, nullptr,
                                 false, ICIS_NoInit));
   RD->addDecl(FieldDecl::Create(Ctx, RD, SourceLocation(), SourceLocation(),
                                 nullptr, Ctx.getObjCClassType(), nullptr,
                                 nullptr, false, ICIS_NoInit));
   RD->completeDefinition();
   
   SuperCTy = Ctx.getTagDeclType(RD);
   SuperPtrCTy = Ctx.getPointerType(SuperCTy);
   
   SuperTy = cast<llvm::StructType>(Types.ConvertType(SuperCTy));
   SuperPtrTy = llvm::PointerType::getUnqual(SuperTy);
   
   // struct _prop_t {
   //   char *name;
   //   char *attributes;
   // }
   PropertyTy = llvm::StructType::create("struct._prop_t",
                                         Int8PtrTy, Int8PtrTy, nullptr);
   
   // struct _prop_list_t {
   //   uint32_t entsize;      // sizeof(struct _prop_t)
   //   uint32_t count_of_properties;
   //   struct _prop_t prop_list[count_of_properties];
   // }
   PropertyListTy =
   llvm::StructType::create("struct._prop_list_t", IntTy, IntTy,
                            llvm::ArrayType::get(PropertyTy, 0), nullptr);
   // struct _prop_list_t *
   PropertyListPtrTy = llvm::PointerType::getUnqual(PropertyListTy);
   
//struct _mulle_objc_method_descriptor
//{
//   mulle_objc_method_id_t   method_id;
//   char                     *name;
//   char                     *signature;
//   unsigned int             bits;  
//};
//struct _mulle_objc_method
//{
//   struct _mulle_objc_method_descriptor  descriptor;
//   mulle_objc_method_implementation_t    implementation;
//};

    MethodTy = llvm::StructType::create("struct._mulle_objc_method",
                                       SelectorIDTy, Int8PtrTy, Int8PtrTy, IntTy, Int8PtrTy,
                                       nullptr);
   
   // struct _objc_cache *
   CacheTy = llvm::StructType::create(VMContext, "struct._objc_cache");
   CachePtrTy = llvm::PointerType::getUnqual(CacheTy);
   
}

ObjCTypesHelper::ObjCTypesHelper(CodeGen::CodeGenModule &cgm)
: ObjCCommonTypesHelper(cgm) {
   // struct _objc_method_description {
   //   SEL name;
   //   char *types;
   // }
   MethodDescriptionTy =
   llvm::StructType::create("struct._objc_method_description",
                            SelectorIDTy, Int8PtrTy, nullptr);
   
   // struct _objc_method_description_list {
   //   int count;
   //   struct _objc_method_description[1];
   // }
   MethodDescriptionListTy = llvm::StructType::create(
                                                      "struct._objc_method_description_list", IntTy,
                                                      llvm::ArrayType::get(MethodDescriptionTy, 0), nullptr);
   
   // struct _objc_method_description_list *
   MethodDescriptionListPtrTy =
   llvm::PointerType::getUnqual(MethodDescriptionListTy);
   
   // Protocol description structures
   
   // struct _objc_protocol_extension {
   //   uint32_t size;  // sizeof(struct _objc_protocol_extension)
   //   struct _objc_method_description_list *optional_instance_methods;
   //   struct _objc_method_description_list *optional_class_methods;
   //   struct _objc_property_list *instance_properties;
   //   const char ** extendedMethodTypes;
   // }
   ProtocolExtensionTy =
   llvm::StructType::create("struct._objc_protocol_extension",
                            IntTy, MethodDescriptionListPtrTy,
                            MethodDescriptionListPtrTy, PropertyListPtrTy,
                            Int8PtrPtrTy, nullptr);
   
   // struct _objc_protocol_extension *
   ProtocolExtensionPtrTy = llvm::PointerType::getUnqual(ProtocolExtensionTy);
   
   // Handle recursive construction of Protocol and ProtocolList types
   
   ProtocolTy =
   llvm::StructType::create(VMContext, "struct._objc_protocol");
   
   ProtocolListTy =
   llvm::StructType::create(VMContext, "struct._objc_protocol_list");
   ProtocolListTy->setBody(llvm::PointerType::getUnqual(ProtocolListTy),
                           LongTy,
                           llvm::ArrayType::get(ProtocolTy, 0),
                           nullptr);
   
   // struct _objc_protocol {
   //   struct _objc_protocol_extension *isa;
   //   char *protocol_name;
   //   struct _objc_protocol **_objc_protocol_list;
   //   struct _objc_method_description_list *instance_methods;
   //   struct _objc_method_description_list *class_methods;
   // }
   ProtocolTy->setBody(ProtocolExtensionPtrTy, Int8PtrTy,
                       llvm::PointerType::getUnqual(ProtocolListTy),
                       MethodDescriptionListPtrTy,
                       MethodDescriptionListPtrTy,
                       nullptr);
   
   // struct _objc_protocol_list *
   ProtocolListPtrTy = llvm::PointerType::getUnqual(ProtocolListTy);
   
   ProtocolPtrTy = llvm::PointerType::getUnqual(ProtocolTy);
   
   // Class description structures
   
   // struct _objc_ivar {
   //   char *ivar_name;
   //   char *ivar_type;
   //   int  ivar_offset;
   // }
   IvarTy = llvm::StructType::create("struct._objc_ivar",
                                     Int8PtrTy, Int8PtrTy, IntTy, nullptr);
   
   // struct _objc_ivar_list *
   IvarListTy =
   llvm::StructType::create(VMContext, "struct._objc_ivar_list");
   IvarListPtrTy = llvm::PointerType::getUnqual(IvarListTy);
   
   // struct _objc_method_list *
   MethodListTy =
   llvm::StructType::create(VMContext, "struct._mulle_objc_method_list");
   MethodListPtrTy = llvm::PointerType::getUnqual(MethodListTy);
   
   // struct _objc_class_extension *
   ClassExtensionTy =
   llvm::StructType::create("struct._objc_class_extension",
                            IntTy, Int8PtrTy, PropertyListPtrTy, nullptr);
   ClassExtensionPtrTy = llvm::PointerType::getUnqual(ClassExtensionTy);
   
   ClassTy = llvm::StructType::create(VMContext, "struct._mulle_objc_load_class");
   
  
//   struct _mulle_objc_load_class
//   {
//      mulle_objc_class_id_t            class_id;
//      char                             *class_name;
//
//      mulle_objc_class_id_t            superclass_unique_id;
//      char                             *superclass_name;
//      
//      size_t                           instance_size;
//
//      struct _mulle_objc_method_list   *class_methods;
//      struct _mulle_objc_method_list   *instance_methods;
//      
//      mulle_objc_protocol_id_t         *protocol_unique_ids;
//   };

   ClassTy->setBody(
                    ClassIDTy,   // class_id
                    Int8PtrTy,   // class_name
                    
                    ClassIDTy,   // superclass_unique_id
                    Int8PtrTy,   // superclass_name,
                    
                    LongTy,      // instance_size

                    MethodListPtrTy,   // class_methods
                    MethodListPtrTy,   // instance_methods
                    
                    ProtocolIDPtrTy,
                    nullptr);
   
   ClassPtrTy = llvm::PointerType::getUnqual(ClassTy);
   
   
//   struct _mulle_objc_load_category
//   {
//      mulle_objc_class_id_t            class_id;
//      char                             *class_name;         // useful ??
//      
//      struct _mulle_objc_method_list   *class_methods;
//      struct _mulle_objc_method_list   *instance_methods;
//      
//      mulle_objc_protocol_id_t         *protocol_unique_ids;
//   };

   CategoryTy =
   llvm::StructType::create("struct._mulle_objc_load_category",
                            ClassIDTy, Int8PtrTy, MethodListPtrTy,
                            MethodListPtrTy, ProtocolIDPtrTy, nullptr);
   
   // Global metadata structures
   
   // struct _objc_symtab {
   //   long sel_ref_cnt;
   //   SEL *refs;
   //   short cls_def_cnt;
   //   short cat_def_cnt;
   //   char *defs[cls_def_cnt + cat_def_cnt];
   // }
   SymtabTy =
   llvm::StructType::create("struct._objc_symtab",
                            LongTy, SelectorIDTy, ShortTy, ShortTy,
                            llvm::ArrayType::get(Int8PtrTy, 0), nullptr);
   SymtabPtrTy = llvm::PointerType::getUnqual(SymtabTy);
   
   // struct _objc_module {
   //   long version;
   //   long size;   // sizeof(struct _objc_module)
   //   char *name;
   //   struct _objc_symtab* symtab;
   //  }
   ModuleTy =
   llvm::StructType::create("struct._objc_module",
                            LongTy, LongTy, Int8PtrTy, SymtabPtrTy, nullptr);
   
   
   // FIXME: This is the size of the setjmp buffer and should be target
   // specific. 18 is what's used on 32-bit X86.
   uint64_t SetJmpBufferSize = 18;
   
   // Exceptions
   llvm::Type *StackPtrTy = llvm::ArrayType::get(CGM.Int8PtrTy, 4);
   
   ExceptionDataTy =
   llvm::StructType::create("struct._objc_exception_data",
                            llvm::ArrayType::get(CGM.Int32Ty,SetJmpBufferSize),
                            StackPtrTy, nullptr);
   
}



CGObjCRuntime *clang::CodeGen::CreateMulleObjCRuntime(CodeGenModule &CGM) 
{
  switch (CGM.getLangOpts().ObjCRuntime.getKind()) {
  case ObjCRuntime::Mulle:
    return new CGObjCMulleRuntime(CGM);

  case ObjCRuntime::MacOSX :
  case ObjCRuntime::FragileMacOSX :
  case ObjCRuntime::iOS :
  case ObjCRuntime::GCC :
  case ObjCRuntime::GNUstep :
  case ObjCRuntime::ObjFW :
    llvm_unreachable("these runtimes are not Mulle ObjC runtimes");
  }
  llvm_unreachable("bad runtime");
}

