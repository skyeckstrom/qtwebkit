/*
 * Copyright (C) 2015 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */

#ifndef B3ConstDoubleValue_h
#define B3ConstDoubleValue_h

#if ENABLE(B3_JIT)

#include "B3Value.h"

namespace JSC { namespace B3 {

class ConstDoubleValue : public Value {
public:
    static bool accepts(Opcode opcode) { return opcode == ConstDouble; }
    
    ~ConstDoubleValue();
    
    double value() const { return m_value; }

    Value* negConstant(Procedure& proc) const override;
    Value* addConstant(Procedure& proc, int32_t other) const override;
    Value* addConstant(Procedure& proc, Value* other) const override;
    Value* subConstant(Procedure& proc, Value* other) const override;

    TriState equalConstant(Value* other) const override;
    TriState notEqualConstant(Value* other) const override;
    TriState lessThanConstant(Value* other) const override;
    TriState greaterThanConstant(Value* other) const override;
    TriState lessEqualConstant(Value* other) const override;
    TriState greaterEqualConstant(Value* other) const override;

protected:
    void dumpMeta(CommaPrinter&, PrintStream&) const override;

private:
    friend class Procedure;

    ConstDoubleValue(unsigned index, Origin origin, double value)
        : Value(index, CheckedOpcode, ConstDouble, Double, origin)
        , m_value(value)
    {
    }
    
    double m_value;
};

} } // namespace JSC::B3

#endif // ENABLE(B3_JIT)

#endif // B3ConstDoubleValue_h

