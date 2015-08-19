#include "llvm_codegen.h"

#include <iostream>
#include <utility>

#include "llvm/IR/Module.h"
#include "llvm/IR/IRBuilder.h"

#include "llvm/Support/raw_ostream.h"

using namespace std;
using namespace simit::ir;

namespace simit {
namespace backend {

llvm::ConstantInt *llvmInt(long long int val, unsigned bits) {
  return llvm::ConstantInt::get(LLVM_CONTEXT, llvm::APInt(bits, val, true));
}

llvm::ConstantInt *llvmUInt(long long unsigned int val, unsigned bits) {
  return llvm::ConstantInt::get(LLVM_CONTEXT, llvm::APInt(bits, val, false));
}

llvm::Constant *llvmFP(double val, unsigned bits) {
  return llvm::ConstantFP::get(getLLVMFloatType(), val);
}

llvm::Constant* llvmBool(bool val) {
  int intVal = (val) ? 1 : 0;
  return llvm::ConstantInt::get(LLVM_CONTEXT, llvm::APInt(1, intVal, false));
}

llvm::Type *llvmPtrType(ScalarType stype, unsigned addrspace) {
  switch (stype.kind) {
    case ScalarType::Int:
      return llvm::Type::getInt32PtrTy(LLVM_CONTEXT, addrspace);
    case ScalarType::Float:
      return getLLVMFloatPtrType(addrspace);
    case ScalarType::Boolean:
      return llvm::Type::getInt1PtrTy(LLVM_CONTEXT, addrspace);
  }
  unreachable;
  return nullptr;
}

llvm::Constant *llvmPtr(llvm::Type *type, const void *data) {
  llvm::Constant *c = (sizeof(void*) == 4)
      ? llvm::ConstantInt::get(llvm::Type::getInt32Ty(LLVM_CONTEXT),
                               (int)(intptr_t)data)
      : llvm::ConstantInt::get(llvm::Type::getInt64Ty(LLVM_CONTEXT),
                               (intptr_t)data);
  return llvm::ConstantExpr::getIntToPtr(c, type);
}

llvm::Constant *llvmPtr(const Type &type, const void *data,
                        unsigned addrspace) {
  return llvmPtr(createLLVMType(type, addrspace), data);
}

llvm::Constant *llvmPtr(const Literal *literal) {
  iassert(literal->type.isTensor());
  return llvmPtr(literal->type, literal->data);
}

llvm::Constant *llvmVal(const Type &type, const void *data) {
  ScalarType componentType = type.toTensor()->componentType;
  switch (componentType.kind) {
    case ScalarType::Int:
      return llvmInt(static_cast<const int*>(data)[0]);
    case ScalarType::Float:
      return llvmFP(static_cast<const double*>(data)[0], componentType.bytes());
    case ScalarType::Boolean:
      return llvmBool(static_cast<const bool*>(data)[0]);
  }
  ierror;
  return nullptr;
}

llvm::Constant *llvmVal(const Literal *literal) {
  return llvmVal(literal->type, literal->data);
}

Type simitType(const llvm::Type *type) {
  if (type->isPointerTy()) {
    type = type->getPointerElementType();
  }

  if ((ScalarType::singleFloat() && type->isFloatTy()) ||
      (!ScalarType::singleFloat() && type->isDoubleTy())) {
    return Float;
  }
  else if (type->isIntegerTy()) {
    return Int;
  }

  unreachable;
  return Type();
}

/// One for endpoints, two for neighbor index
extern const int NUM_EDGE_INDEX_ELEMENTS = 3;

llvm::Type *getLLVMFloatType() {
  if (ScalarType::singleFloat()) {
    return LLVM_FLOAT;
  }
  else {
    return LLVM_DOUBLE;
  }
}

llvm::Type *getLLVMFloatPtrType(unsigned addrspace) {
  if (ScalarType::singleFloat()) {
    return llvm::Type::getFloatPtrTy(LLVM_CONTEXT, addrspace);
  }
  else {
    return llvm::Type::getDoublePtrTy(LLVM_CONTEXT, addrspace);
  }
}

llvm::Type *createLLVMType(const Type &type, unsigned addrspace) {
  switch (type.kind()) {
    case Type::Tensor:
      return createLLVMType(type.toTensor(), addrspace);
    case Type::Element:
      not_supported_yet;
      break;
    case Type::Set:
      return createLLVMType(type.toSet(), addrspace);
    case Type::Tuple:
      not_supported_yet;
      break;
  }
  unreachable;
  return nullptr;
}

// TODO: replace anonymous struct with one struct per element and set type
llvm::StructType *createLLVMType(const ir::SetType *setType,
                                 unsigned addrspace) {
  const ElementType *elemType = setType->elementType.toElement();
  vector<llvm::Type*> llvmFieldTypes;

  // Set size
  llvmFieldTypes.push_back(LLVM_INT);

  // Edge indices (if the set is an edge set)
  if (setType->endpointSets.size() > 0) {
    // Endpoints
    llvmFieldTypes.push_back(
        llvm::Type::getInt32PtrTy(LLVM_CONTEXT, addrspace));

    // Neighbor Index
    // row starts (block row)
    llvmFieldTypes.push_back(
        llvm::Type::getInt32PtrTy(LLVM_CONTEXT, addrspace));
    // col indexes (block column)
    llvmFieldTypes.push_back(
        llvm::Type::getInt32PtrTy(LLVM_CONTEXT, addrspace));
  }

  // Fields
  for (const Field &field : elemType->fields) {
    llvmFieldTypes.push_back(createLLVMType(field.type, addrspace));
  }
  return llvm::StructType::get(LLVM_CONTEXT, llvmFieldTypes);
}

llvm::Type *createLLVMType(const TensorType *type, unsigned addrspace) {
  return llvmPtrType(type->componentType, addrspace);
}

llvm::Type *createLLVMType(ScalarType stype) {
  switch (stype.kind) {
    case ScalarType::Int:
      return LLVM_INT;
    case ScalarType::Float:
      return getLLVMFloatType();
    case ScalarType::Boolean:
      return LLVM_BOOL;
  }
  unreachable;
  return nullptr;
}

static llvm::Function *createPrototype(const std::string &name,
                                       const vector<string> &argNames,
                                       const vector<llvm::Type*> &argTypes,
                                       llvm::Module *module,
                                       bool externalLinkage,
                                       bool doesNotThrow) {
  llvm::FunctionType *ft = llvm::FunctionType::get(LLVM_VOID, argTypes, false);
  auto linkage = externalLinkage ? llvm::Function::ExternalLinkage
                                 : llvm::Function::InternalLinkage;
  llvm::Function *f= llvm::Function::Create(ft, linkage, name, module);
  if (doesNotThrow) {
    f->setDoesNotThrow();
  }
  unsigned i = 0;
  for (llvm::Argument &arg : f->getArgumentList()) {
    arg.setName(argNames[i]);

    // TODO(gkanwar): Move noalias code here from GPU implementation
    if (arg.getType()->isPointerTy()) {
      f->setDoesNotCapture(i+1);  //  setDoesNotCapture(0) is the return value
    }
    ++i;
  }

  return f;
}

llvm::Function *createPrototype(const std::string &name,
                                const vector<Var> &arguments,
                                const vector<Var> &results,
                                llvm::Module *module,
                                bool externalLinkage,
                                bool doesNotThrow,
                                bool scalarsByValue,
                                unsigned addrspace) {
  vector<string>      llvmArgNames;
  vector<llvm::Type*> llvmArgTypes;

  // We don't need two llvm arguments for aliased simit argument/results
  std::set<std::string> argNames;
  
  for (auto &arg : arguments) {
    argNames.insert(arg.getName());
    llvmArgNames.push_back(arg.getName());

    // Our convention is that scalars are passed to functions by value,
    // while everything else is passed through a pointer
    llvm::Type *llvmType = (isScalar(arg.getType()) && scalarsByValue)
        ? createLLVMType(arg.getType().toTensor()->componentType)
        : createLLVMType(arg.getType(), addrspace);
    llvmArgTypes.push_back(llvmType);
  }

  for (auto &res : results) {
    if (argNames.find(res.getName()) != argNames.end()) {
      continue;
    }
    llvmArgNames.push_back(res.getName());
    llvmArgTypes.push_back(createLLVMType(res.getType(), addrspace));
  }

  assert(llvmArgNames.size() == llvmArgTypes.size());

  return createPrototype(name, llvmArgNames, llvmArgTypes,
                         module, externalLinkage, doesNotThrow);
}

std::ostream &operator<<(std::ostream &os, const llvm::Value &value) {
  std::string str;
  llvm::raw_string_ostream ss(str);
  value.print(ss);
  return os << ss.str();
}

std::ostream &operator<<(std::ostream &os, const llvm::Type &type) {
  std::string str;
  llvm::raw_string_ostream ss(str);
  type.print(ss);
  return os << ss.str();
}

std::ostream &operator<<(std::ostream &os, const llvm::Module &module) {
  std::string fstr;
  llvm::raw_string_ostream rsos(fstr);
  module.print(rsos, nullptr);
  os << rsos.str();
  return os;
}

}}