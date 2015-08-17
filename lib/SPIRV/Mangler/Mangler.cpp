//===--------------------------- Mangler.cpp -----------------------------===//
//
//                              SPIR Tools
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===---------------------------------------------------------------------===//
/*
 * Contributed by: Intel Corporation.
 */

#include "FunctionDescriptor.h"
#include "ManglingUtils.h"
#include "NameMangleAPI.h"
#include "ParameterType.h"
#include <string>
#include <sstream>
#include <map>

namespace SPIR {

class MangleVisitor: public TypeVisitor {
public:

  MangleVisitor(SPIRversion ver, std::stringstream& s) : TypeVisitor(ver), m_stream(s), seqId(0) {
  }

//
// mangle substitution methods
//
  void mangleSequenceID(unsigned SeqID) {
    if (SeqID == 1)
      m_stream << '0';
    else if (SeqID > 1) {
      std::string bstr;
      std::string charset = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
      SeqID--;
      bstr.reserve(7);
      for (; SeqID != 0; SeqID /= 36)
        bstr += charset.substr(SeqID % 36, 1);
      std::reverse(bstr.begin(), bstr.end());
      m_stream << bstr;
    }
    m_stream << '_';
  }

  bool mangleSubstitution(const ParamType* type, std::string typeStr) {
    size_t fpos;
    std::stringstream thistypeStr;
    thistypeStr << typeStr;
    if ((fpos = m_stream.str().find(typeStr)) != std::string::npos) {
      const char* nType;
      if (const PointerType* p = SPIR::dyn_cast<PointerType>(type)) {
        if ((nType = mangledPrimitiveStringfromName(p->getPointee()->toString())))
          thistypeStr << nType;
      }
#if defined(AMD_OPENCL) || 1
      else if (const VectorType* pVec = SPIR::dyn_cast<VectorType>(type)) {
        if ((nType = mangledPrimitiveStringfromName(pVec->getScalarType()->toString())))
          thistypeStr << nType;
      }
#endif
      std::map<std::string, unsigned>::iterator I = substitutions.find(thistypeStr.str());
      if (I == substitutions.end())
        return false;

      unsigned SeqID = I->second;
      m_stream << 'S';
      mangleSequenceID(SeqID);
      return true;
    }
    return false;
  }

//
// Visit methods
//
  MangleError visit(const PrimitiveType* t) {
    m_stream << mangledPrimitiveString(t->getPrimitive());
    return MANGLE_SUCCESS;
  }

  MangleError visit(const PointerType* p) {
    size_t fpos = m_stream.str().size();
    std::stringstream typeStr;
    MangleError me = MANGLE_SUCCESS;
    typeStr << 'P';
    for (unsigned int i = ATTR_QUALIFIER_FIRST; i <= ATTR_QUALIFIER_LAST; i++) {
      TypeAttributeEnum qualifier = (TypeAttributeEnum)i;
      if (p->hasQualifier(qualifier)) {
        typeStr << getMangledAttribute(qualifier);
      }
    }
    typeStr << getMangledAttribute((p->getAddressSpace()));
    if (!mangleSubstitution(p, typeStr.str())) {
      m_stream << typeStr.str();
      size_t tpos = m_stream.str().size();
      me = p->getPointee()->accept(this);
      typeStr.str(std::string());
      typeStr << 'P' << m_stream.str().substr(tpos);
      if (typeStr.str().find('S') == std::string::npos) {
        substitutions[typeStr.str()] = seqId++;
        substitutions[m_stream.str().substr(fpos)] = seqId++;
      }
    }
    return me;
  }

  MangleError visit(const VectorType* v) {
    size_t index = m_stream.str().size();
    std::stringstream typeStr;
    typeStr << "Dv" << v->getLength() << "_";
    MangleError me = MANGLE_SUCCESS;
    // According to IA64 name mangling spec,
    // builtin types should not be substituted
    // This is a workaround till this gets fixed in CLang
#if defined(AMD_OPENCL) || 1
    if (!mangleSubstitution(v, typeStr.str()))
#endif
    {
      m_stream << typeStr.str();
      MangleError me = v->getScalarType()->accept(this);
      substitutions[m_stream.str().substr(index)] = seqId++;
    }
    return me;
  }

  MangleError visit(const AtomicType* p) {
    m_stream << "U" << "7_Atomic";
    return p->getBaseType()->accept(this);
  }

  MangleError visit(const BlockType* p) {
    m_stream << "U" << "13block_pointerFv";
    if (p->getNumOfParams() == 0)
      m_stream << "v";
    else
      for (unsigned int i=0; i < p->getNumOfParams(); ++i) {
        MangleError err = p->getParam(i)->accept(this);
        if (err != MANGLE_SUCCESS) {
          return err;
        }
      }
    m_stream << "E";
    return MANGLE_SUCCESS;
  }

  MangleError visit(const UserDefinedType* pTy) {
    std::string name = pTy->toString();
    m_stream << name.size() << name;
    return MANGLE_SUCCESS;
  }

private:

  // Holds the mangled string representing the prototype of the function.
  std::stringstream& m_stream;
  unsigned seqId;
  std::map<std::string, unsigned> substitutions;
};

//
// NameMangler
//
  NameMangler::NameMangler(SPIRversion version):m_spir_version(version) {};

  MangleError NameMangler::mangle(const FunctionDescriptor& fd, std::string& mangledName ) {
    if (fd.isNull()) {
      mangledName.assign(FunctionDescriptor::nullString());
      return MANGLE_NULL_FUNC_DESCRIPTOR;
    }
    std::stringstream ret;
    ret << "_Z" << fd.name.length() << fd.name;
    MangleVisitor visitor(m_spir_version, ret);
    for (unsigned int i=0; i < fd.parameters.size(); ++i) {
      MangleError err = fd.parameters[i]->accept(&visitor);
      if(err == MANGLE_TYPE_NOT_SUPPORTED) {
        mangledName.assign("Type ");
        mangledName.append(fd.parameters[i]->toString());
        mangledName.append(" is not supported in ");
        std::string ver = getSPIRVersionAsString(m_spir_version);
        mangledName.append(ver);
        return err;
      }
    }
    mangledName.assign(ret.str());
    return MANGLE_SUCCESS;
  }

} // End SPIR namespace
