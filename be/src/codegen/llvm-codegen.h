// Copyright (c) 2012 Cloudera, Inc.  All right reserved.

#ifndef IMPALA_CODEGEN_LLVM_CODEGEN_H
#define IMPALA_CODEGEN_LLVM_CODEGEN_H

#include "common/status.h"

#include <map>
#include <string>
#include <vector>
#include <boost/scoped_ptr.hpp>
#include <boost/thread/mutex.hpp>

#include <llvm/DerivedTypes.h>
#include <llvm/Intrinsics.h>
#include <llvm/LLVMContext.h>
#include <llvm/Module.h>
#include <llvm/Support/IRBuilder.h>
#include <llvm/Analysis/Verifier.h>

#include "exprs/expr.h"
#include "impala-ir/impala-ir-functions.h"
#include "runtime/primitive-type.h"
#include "util/runtime-profile.h"

// Forward declare all llvm classes to avoid namespace pollution.  
namespace llvm {
  class AllocaInst;
  class BasicBlock;
  class ConstantFolder;
  class ExecutionEngine;
  class Function;
  class FunctionPassManager;
  class LLVMContext;
  class Module;
  class NoFolder;
  class PassManager;
  class PointerType;
  class StructType;
  class TargetData;
  class Type;
  class Value;

  template<bool B, typename T, typename I>
  class IRBuilder;

  template<bool preserveName>
  class IRBuilderDefaultInserter;
}

namespace impala {

class SubExprElimination;

// LLVM code generator.  This is the top level object to generate jitted code.  
//
// LLVM provides a c++ IR builder interface so IR does not need to be written
// manually.  The interface is very low level so each line of IR that needs to
// be output maps 1:1 with calls to the interface.
// The llvm documentation is not fantastic and a lot of this was figured out
// by experimenting.  Thankfully, their API is pretty well designed so it's
// possible to get by without great documentation.  The llvm tutorial is very
// helpful, http://llvm.org/docs/tutorial/LangImpl1.html.  In this tutorial, they
// go over how to JIT an AST for a toy language they create.
// It is also helpful to use their online app that lets you compile c/c++ to IR.
// http://llvm.org/demo/index.cgi.  
//
// This class provides two interfaces, one for testing and one for the query
// engine.  The interface for the query engine will load the cross-compiled
// IR module (output during the build) and extract all of functions that will
// be called directly.  The test interface can be used to load any precompiled 
// module or none at all (but this class will not validate the module).
//
// This class is mostly not threadsafe.  During the Prepare() phase of the fragment
// execution, nodes should codegen functions.
// Afterward, OptimizeModule() should be called at which point all codegened functions
// are optimized.  
// Subsequently, nodes can get at the jit compiled function pointer (typically during the 
// Open() call).  Getting the jit compiled function (JitFunction()) is the only thread 
// safe function.
//
// Currently, each query will create and initialize one of these 
// objects.  This requires loading and parsing the cross compiled modules.
// TODO: we should be able to do this once per process and let llvm compile
// functions from across modules.
//
// LLVM has a nontrivial memory management scheme and objects will take
// ownership of others.  The document is pretty good about being explicit with this
// but it is not very intuitive.
// TODO: look into diagnostic output and debuggability
// TODO: confirm that the multi-threaded usage is correct
class LlvmCodeGen {
 public:
  // This function must be called once per process before any llvm API calls are
  // made.  LLVM needs to allocate data structures for multi-threading support and
  // to enable dynamic linking of jitted code.
  // if 'load_backend', load the backend static object for llvm.  This is needed
  // when libbackend.so is loaded from java.  llvm will be default only look in
  // the current object and not be able to find the backend symbols 
  // TODO: this can probably be removed after impalad refactor where the java
  // side is not loading the be explicitly anymore.
  static void InitializeLlvm(bool load_backend = false);

  // Loads and parses the precompiled impala IR module
  // codegen will contain the created object on success.  
  static Status LoadImpalaIR(ObjectPool*, boost::scoped_ptr<LlvmCodeGen>* codegen);

  // Removes all jit compiled dynamically linked functions from the process.
  ~LlvmCodeGen();

  RuntimeProfile* runtime_profile() { return &profile_; }
  RuntimeProfile::Counter* codegen_timer() { return codegen_timer_; }

  // Turns on/off optimization passes
  void EnableOptimizations(bool enable);

  // For debugging. Returns the IR that was generated.  If full_module, the
  // entire module is dumped, including what was loaded from precompiled IR.
  // If false, only output IR for functions which were generated.
  std::string GetIR(bool full_module) const;

  // Typedef builder in case we want to change the template arguments later
  typedef llvm::IRBuilder<> LlvmBuilder;

  // Utility struct that wraps a variable name and llvm type.
  struct NamedVariable {
    std::string name;
    llvm::Type* type;

    NamedVariable(const std::string& name="", llvm::Type* type = NULL) {
      this->name = name;
      this->type = type;
    }
  };
  
  // Abstraction over function prototypes.  Contains helpers to build prototypes and
  // generate IR for the types.  
  class FnPrototype {
   public:
    // Create a function prototype object, specifying the name of the function and
    // the return type.
    FnPrototype(LlvmCodeGen*, const std::string& name, llvm::Type* ret_type);

    // Returns name of function
    const std::string& name() const { return name_; }

    // Add argument
    void AddArgument(const NamedVariable& var) {
      args_.push_back(var);
    }

    // Generate LLVM function prototype. 
    // If a non-null builder is passed, this function will also create the entry block
    // and set the builder's insert point to there.
    // If params is non-null, this function will also return the arguments 
    // values (params[0] is the first arg, etc). 
    // In that case, params should be preallocated to be number of arguments
    llvm::Function* GeneratePrototype(LlvmBuilder* builder = NULL,
        llvm::Value** params = NULL);

   private:
    friend class LlvmCodeGen;

    LlvmCodeGen* codegen_;
    std::string name_;
    llvm::Type* ret_type_;
    std::vector<NamedVariable> args_;
  };

  // Returns llvm type for the primitive type
  llvm::Type* GetType(PrimitiveType type);

  // Return a pointer type to 'type' (e.g. int16_t*)
  llvm::PointerType* GetPtrType(PrimitiveType type);

  // Returns the type with 'name'.  This is used to pull types from clang
  // compiled IR.  The types we generate at runtime are unnamed.
  // The name is generated by the clang compiler in this form:
  // <class/struct>.<namespace>::<class name>.  For example:
  // "class.impala::AggregationNode"
  llvm::Type* GetType(const std::string& name);

  // Returns reference to llvm context object.  Each LlvmCodeGen has its own
  // context to allow multiple threads to be calling into llvm at the same time.
  llvm::LLVMContext& context() { return *context_.get(); }

  // Returns execution engine interface
  llvm::ExecutionEngine* execution_engine() { return execution_engine_.get(); }

  // Returns the underlying llvm module
  llvm::Module* module() { return module_; }

  // Register a expr function with unique id.  It can be subsequently retrieved via
  // GetRegisteredExprFn with that id.  
  void RegisterExprFn(int64_t id, llvm::Function* function) {
    DCHECK(registered_exprs_map_.find(id) == registered_exprs_map_.end());
    registered_exprs_map_[id] = function;
    registered_exprs_.insert(function);
  }

  // Returns a registered expr function for id or NULL if it does not exist.
  llvm::Function* GetRegisteredExprFn(int64_t id) {
    std::map<int64_t, llvm::Function*>::iterator it = registered_exprs_map_.find(id);
    if (it == registered_exprs_map_.end()) return NULL;
    return it->second;
  }

  // Optimize the entire module.  LLVM is more built for running its optimization
  // passes over the entire module (all the functions) rather than individual
  // functions.
  Status OptimizeModule();

  // Replaces all instructions that call 'target_name' with a call instruction
  // to the new_fn.  Returns the modified function.
  // - target_name is the unmangled function name that should be replaced.
  //   The name is assumed to be unmangled so all call sites that contain the
  //   replace_name substring will be replaced. target_name is case-sensitive
  //   TODO: be more strict than substring? work out the mangling rules? 
  // - If update_in_place is true, the caller function will be modified in place.
  //   Otherwise, the caller function will be cloned and the original function
  //   is unmodified.  If update_in_place is false and the function is already
  //   been dynamically linked, the existing function will be unlinked. Note that
  //   this is very unthread-safe, if there are threads in the function to be unlinked,
  //   bad things will happen.
  // - 'num_replaced' returns the number of call sites updated
  //
  // Most of our use cases will likely not be in place.  We will have one 'template'
  // version of the function loaded for each type of Node (e.g. AggregationNode).
  // Each instance of the node will clone the function, replacing the inner loop
  // body with the codegened version.  The codegened bodies differ from instance
  // to instance since they are specific to the node's tuple desc.
  llvm::Function* ReplaceCallSites(llvm::Function* caller, bool update_in_place,
      llvm::Function* new_fn, const std::string& target_name, int* num_replaced);

  // Verify and optimize function.  This should be called at the end for each
  // codegen'd function.  If the function does not verify, it will return NULL,
  // otherwise, it will optimize, mark the function for inlining and return the
  // function object.
  llvm::Function* FinalizeFunction(llvm::Function* function);
  
  // Inline all function calls for 'fn'.  'fn' is modified in place.  Returns
  // the number of functions inlined.  This is *not* called recursively
  // (i.e. second level function calls are not inlined).  This can be called
  // again to inline those until this returns 0.
  int InlineAllCallSites(llvm::Function* fn, bool skip_registered_fns);

  // Optimizes the function in place.  This uses a combination of llvm optimization 
  // passes as well as some custom heuristics.  This should be called for all 
  // functions which call Exprs.  The exprs will be inlined as much as possible,
  // and will do basic sub expression elimination.
  // This should be called before OptimizeModule for functions that want to remove
  // redundant exprs.  This should be called at the highest level possible to 
  // maximize the number of redundant exprs that can be found.
  // TODO: we need to spend more time to output better IR.  Asking llvm to 
  // remove redundant codeblocks on its own is too difficult for it.
  // TODO: this should implement the llvm FunctionPass interface and integrated
  // with the llvm optimization passes.
  llvm::Function* OptimizeFunctionWithExprs(llvm::Function* fn);

  // Jit compile the function.  This will run optimization passes and verify 
  // the function.  The result is a function pointer that is dynamically linked
  // into the process. 
  // Returns NULL if the function is invalid.
  // scratch_size will be set to the buffer size required to call the function
  // scratch_size is the total size from all LlvmCodeGen::GetScratchBuffer
  // calls (with some additional bytes for alignment)
  // This function is thread safe.
  void* JitFunction(llvm::Function* function, int* scratch_size = NULL);

  // Verfies the function if the verfier is enabled.  Returns false if function
  // is invalid.
  bool VerifyFunction(llvm::Function* function);

  // This will generate a printf call instruction to output 'message' at the 
  // builder's insert point.  Only for debugging.
  void CodegenDebugTrace(LlvmBuilder* builder, const char* message);

  // Returns the libc function, adding it to the module if it has not already been.
  llvm::Function* GetLibCFunction(FnPrototype* prototype);

  // Returns the cross compiled function. IRFunction::Type is an enum which is
  // defined in 'impala-ir/impala-ir-functions.h'
  llvm::Function* GetFunction(IRFunction::Type);

  // Returns the hash function with signature:
  //   int32_t Hash(int8_t* data, int len, int32_t seed);
  // If num_bytes is non-zero, the returned function will be codegen'd to only
  // work for that number of bytes.  It is invalid to call that function with a 
  // different 'len'.
  llvm::Function* GetHashFunction(int num_bytes = -1);

  // Allocate stack storage for local variables.  This is similar to traditional c, where
  // all the variables must be declared at the top of the function.  This helper can be
  // called from anywhere and will add a stack allocation for 'var' at the beginning of 
  // the function.  This would be used, for example, if a function needed a temporary
  // struct allocated.  The allocated variable is scoped to the function.
  // This is not related to GetScratchBuffer which is used for structs that are returned 
  // to the caller.
  llvm::AllocaInst* CreateEntryBlockAlloca(llvm::Function* f, const NamedVariable& var);

  // Utility to create two blocks in 'fn' for if/else codegen.  if_block and else_block
  // are return parameters.  insert_before is optional and if set, the two blocks
  // will be inserted before that block otherwise, it will be inserted at the end
  // of 'fn'.  Being able to place blocks is useful for debugging so the IR has a
  // better looking control flow.
  void CreateIfElseBlocks(llvm::Function* fn, const std::string& if_name, 
      const std::string& else_name,
      llvm::BasicBlock** if_block, llvm::BasicBlock** else_block, 
      llvm::BasicBlock* insert_before = NULL);

  // Returns offset into scratch buffer: offset points to area of size 'byte_size'
  // Called by expr generation to request scratch buffer.  This is used for struct
  // types (i.e. StringValue) where data cannot be returned by registers.
  // For example, to jit the expr "strlen(str_col)", we need a temporary StringValue
  // struct from the inner SlotRef expr node.  The SlotRef node would call
  // GetScratchBuffer(sizeof(StringValue)) and output the intermediate struct at
  // scratch_buffer (passed in as argument to compute function) + offset. 
  int GetScratchBuffer(int byte_size);

  // Create a llvm pointer value from 'ptr'.  This is used to pass pointers between
  // c-code and code-generated IR.  The resulting value will be of 'type'.
  llvm::Value* CastPtrToLlvmPtr(llvm::Type* type, void* ptr);

  // Returns the constant 'val' of 'type' 
  llvm::Value* GetIntConstant(PrimitiveType type, int64_t val);

  // Returns true/false constants (bool type)
  llvm::Value* true_value() { return true_value_; }
  llvm::Value* false_value() { return false_value_; }
  llvm::Value* null_ptr_value() { return llvm::ConstantPointerNull::get(ptr_type()); }

  // Simple wrappers to reduce code verbosity
  llvm::Type* boolean_type() { return GetType(TYPE_BOOLEAN); }
  llvm::Type* double_type() { return GetType(TYPE_DOUBLE); }
  llvm::Type* bigint_type() { return GetType(TYPE_BIGINT); }
  llvm::PointerType* ptr_type() { return ptr_type_; }
  llvm::Type* void_type() { return void_type_; }

  // Fills 'functions' with all the functions that are defined in the module.
  // Note: this does not include functions that are just declared
  void GetFunctions(std::vector<llvm::Function*>* functions);

  // Generates function to return min/max(v1, v2)
  llvm::Function* CodegenMinMax(PrimitiveType type, bool min);

  // Codegen to call llvm memcpy intrinsic at the current builder location
  // dst & src must be pointer types.  size is the number of bytes to copy.
  void CodegenMemcpy(LlvmBuilder*, llvm::Value* dst, llvm::Value* src, int size);

  // Codegen computing v1 == v2.  Returns the result.  v1 and v2 must be the same type
  llvm::Value* CodegenEquals(LlvmBuilder*, llvm::Value* v1, llvm::Value* v2, 
      PrimitiveType);

  // Codegen for do *dst = src.  For native types, this is just a store, for structs
  // we need to assign the fields one by one
  void CodegenAssign(LlvmBuilder*, llvm::Value* dst, llvm::Value* src, PrimitiveType);

 private:
  friend class LlvmCodeGenTest;
  friend class SubExprElimination;

  // Top level codegen object.  'module_name' is only used for debugging when
  // outputting the IR.  module's loaded from disk will be named as the file
  // path.  
  LlvmCodeGen(ObjectPool* pool, const std::string& module_name);

  // Initializes the jitter and execution engine.  
  Status Init();

  // Load a pre-compiled IR module from 'file'.  This creates a top level
  // codegen object.  This is used by tests to load custom modules.
  // codegen will contain the created object on success.  
  static Status LoadFromFile(ObjectPool*, const std::string& file, 
      boost::scoped_ptr<LlvmCodeGen>* codegen);

  // Load the intrinsics impala needs.  This is a one time initialization.
  // Values are stored in 'llvm_intrinsics_'
  Status LoadIntrinsics();

  // Clears generated hash fns.  This is only used for testing.
  void ClearHashFns();

  // Name of the JIT module.  Useful for debugging.
  std::string name_;

  // Codegen counters
  RuntimeProfile profile_;
  RuntimeProfile::Counter* load_module_timer_;
  RuntimeProfile::Counter* module_file_size_;
  RuntimeProfile::Counter* compile_timer_;
  RuntimeProfile::Counter* codegen_timer_;

  // whether or not optimizations are enabled
  bool optimizations_enabled_;

  // If true, the module is corrupt and we cannot codegen this query. 
  // TODO: we could consider just removing the offending function and attempting to
  // codegen the rest of the query.  This requires more testing though to make sure
  // that the error is recoverable.
  bool is_corrupt_;

  // If true, the module has been compiled.  It is not valid to add additional
  // functions after this point.
  bool is_compiled_;

  // Error string that llvm will write to
  std::string error_string_;

  // Top level llvm object.  Objects from different contexts do not share anything.
  // We can have multiple instances of the LlvmCodeGen object in different threads
  boost::scoped_ptr<llvm::LLVMContext> context_;

  // Top level codegen object.  Contains everything to jit one 'unit' of code.
  // Owned by the execution_engine_.
  llvm::Module* module_;

  // Execution/Jitting engine.  
  boost::scoped_ptr<llvm::ExecutionEngine> execution_engine_;

  // current offset into scratch buffer 
  int scratch_buffer_offset_;

  // Keeps track of all the functions that have been jit compiled and linked into
  // the process. Special care needs to be taken if we need to modify these functions.
  // bool is unused.
  std::map<llvm::Function*, bool> jitted_functions_;
  
  // Lock protecting jitted_functions_
  boost::mutex jitted_functions_lock_;

  // Keeps track of the external functions that have been included in this module
  // e.g libc functions or non-jitted impala functions.
  // TODO: this should probably be FnPrototype->Functions mapping
  std::map<std::string, llvm::Function*> external_functions_;

  // Functions parsed from pre-compiled module.  Indexed by ImpalaIR::Function enum
  std::vector<llvm::Function*> loaded_functions_;

  // Stores functions codegen'd by impala.  This does not contain cross compiled 
  // functions, only function that were generated at runtime.  Does not overlap
  // with loaded_functions_.
  std::vector<llvm::Function*> codegend_functions_;

  // A mapping of unique id to registered expr functions
  std::map<int64_t, llvm::Function*> registered_exprs_map_;

  // A set of all the functions in 'registered_exprs_map_' for quick lookup.
  std::set<llvm::Function*> registered_exprs_;

  // A cache of loaded llvm intrinsics
  std::map<llvm::Intrinsic::ID, llvm::Function*> llvm_intrinsics_;

  // This is a cache of generated hash functions by byte size.  It is common
  // for the caller to know the number of bytes to hash (e.g. tuple width) and
  // we can codegen a loop unrolled hash function.
  std::map<int, llvm::Function*> hash_fns_;

  // Debug utility that will insert a printf-like function into the generated
  // IR.  Useful for debugging the IR.  This is lazily created.
  llvm::Function* debug_trace_fn_;

  // Debug strings that will be outputted by jitted code.  This is a copy of all
  // strings passed to CodegenDebugTrace.
  std::vector<std::string> debug_strings_;

  // llvm representation of a few common types.  Owned by context.
  llvm::PointerType* ptr_type_;             // int8_t*
  llvm::Type* void_type_;                   // void
  llvm::Type* string_val_type_;             // StringVal

  // llvm constants to help with code gen verbosity
  llvm::Value* true_value_;
  llvm::Value* false_value_;
};

}

#endif

