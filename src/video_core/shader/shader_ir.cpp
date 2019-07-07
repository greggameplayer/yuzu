// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cmath>
#include <unordered_map>

#include "common/assert.h"
#include "common/common_types.h"
#include "common/logging/log.h"
#include "video_core/engines/shader_bytecode.h"
#include "video_core/shader/node_helper.h"
#include "video_core/shader/shader_ir.h"

namespace VideoCommon::Shader {

using Tegra::Shader::Attribute;
using Tegra::Shader::Instruction;
using Tegra::Shader::IpaMode;
using Tegra::Shader::Pred;
using Tegra::Shader::PredCondition;
using Tegra::Shader::PredOperation;
using Tegra::Shader::Register;

ShaderIR::ShaderIR(const ProgramCode& program_code, u32 main_offset, const std::size_t size)
    : program_code{program_code}, main_offset{main_offset}, program_size{size} {
    Decode();
}

ShaderIR::~ShaderIR() = default;

Node ShaderIR::GetRegister(Register reg) {
    if (reg != Register::ZeroIndex) {
        used_registers.insert(static_cast<u32>(reg));
    }
    return MakeNode<GprNode>(reg);
}

Node ShaderIR::GetImmediate19(Instruction instr) {
    return Immediate(instr.alu.GetImm20_19());
}

Node ShaderIR::GetImmediate32(Instruction instr) {
    return Immediate(instr.alu.GetImm20_32());
}

Node ShaderIR::GetConstBuffer(u64 index_, u64 offset_) {
    const auto index = static_cast<u32>(index_);
    const auto offset = static_cast<u32>(offset_);

    const auto [entry, is_new] = used_cbufs.try_emplace(index);
    entry->second.MarkAsUsed(offset);

    return MakeNode<CbufNode>(index, Immediate(offset));
}

Node ShaderIR::GetConstBufferIndirect(u64 index_, u64 offset_, Node node) {
    const auto index = static_cast<u32>(index_);
    const auto offset = static_cast<u32>(offset_);

    const auto [entry, is_new] = used_cbufs.try_emplace(index);
    entry->second.MarkAsUsedIndirect();

    const Node final_offset = Operation(OperationCode::UAdd, NO_PRECISE, node, Immediate(offset));
    return MakeNode<CbufNode>(index, final_offset);
}

Node ShaderIR::GetPredicate(u64 pred_, bool negated) {
    const auto pred = static_cast<Pred>(pred_);
    if (pred != Pred::UnusedIndex && pred != Pred::NeverExecute) {
        used_predicates.insert(pred);
    }

    return MakeNode<PredicateNode>(pred, negated);
}

Node ShaderIR::GetPredicate(bool immediate) {
    return GetPredicate(static_cast<u64>(immediate ? Pred::UnusedIndex : Pred::NeverExecute));
}

Node ShaderIR::GetInputAttribute(Attribute::Index index, u64 element, Node buffer) {
    used_input_attributes.emplace(index);
    return MakeNode<AbufNode>(index, static_cast<u32>(element), buffer);
}

Node ShaderIR::GetPhysicalInputAttribute(Tegra::Shader::Register physical_address, Node buffer) {
    uses_physical_attributes = true;
    return MakeNode<AbufNode>(GetRegister(physical_address), buffer);
}

Node ShaderIR::GetOutputAttribute(Attribute::Index index, u64 element, Node buffer) {
    if (index == Attribute::Index::LayerViewportPointSize) {
        switch (element) {
        case 0:
            UNIMPLEMENTED();
            break;
        case 1:
            uses_layer = true;
            break;
        case 2:
            uses_viewport_index = true;
            break;
        case 3:
            uses_point_size = true;
            break;
        }
    }
    if (index == Attribute::Index::ClipDistances0123 ||
        index == Attribute::Index::ClipDistances4567) {
        const auto clip_index =
            static_cast<u32>((index == Attribute::Index::ClipDistances4567 ? 1 : 0) + element);
        used_clip_distances.at(clip_index) = true;
    }
    used_output_attributes.insert(index);

    return MakeNode<AbufNode>(index, static_cast<u32>(element), buffer);
}

Node ShaderIR::GetInternalFlag(InternalFlag flag, bool negated) {
    const Node node = MakeNode<InternalFlagNode>(flag);
    if (negated) {
        return Operation(OperationCode::LogicalNegate, node);
    }
    return node;
}

Node ShaderIR::GetLocalMemory(Node address) {
    return MakeNode<LmemNode>(address);
}

Node ShaderIR::GetTemporal(u32 id) {
    return GetRegister(Register::ZeroIndex + 1 + id);
}

Node ShaderIR::GetOperandAbsNegFloat(Node value, bool absolute, bool negate) {
    if (absolute) {
        value = Operation(OperationCode::FAbsolute, NO_PRECISE, value);
    }
    if (negate) {
        value = Operation(OperationCode::FNegate, NO_PRECISE, value);
    }
    return value;
}

Node ShaderIR::GetSaturatedFloat(Node value, bool saturate) {
    if (!saturate) {
        return value;
    }
    const Node positive_zero = Immediate(std::copysignf(0, 1));
    const Node positive_one = Immediate(1.0f);
    return Operation(OperationCode::FClamp, NO_PRECISE, value, positive_zero, positive_one);
}

Node ShaderIR::ConvertIntegerSize(Node value, Tegra::Shader::Register::Size size, bool is_signed) {
    switch (size) {
    case Register::Size::Byte:
        value = SignedOperation(OperationCode::ILogicalShiftLeft, is_signed, NO_PRECISE, value,
                                Immediate(24));
        value = SignedOperation(OperationCode::IArithmeticShiftRight, is_signed, NO_PRECISE, value,
                                Immediate(24));
        return value;
    case Register::Size::Short:
        value = SignedOperation(OperationCode::ILogicalShiftLeft, is_signed, NO_PRECISE, value,
                                Immediate(16));
        value = SignedOperation(OperationCode::IArithmeticShiftRight, is_signed, NO_PRECISE, value,
                                Immediate(16));
    case Register::Size::Word:
        // Default - do nothing
        return value;
    default:
        UNREACHABLE_MSG("Unimplemented conversion size: {}", static_cast<u32>(size));
        return value;
    }
}

Node ShaderIR::GetOperandAbsNegInteger(Node value, bool absolute, bool negate, bool is_signed) {
    if (!is_signed) {
        // Absolute or negate on an unsigned is pointless
        return value;
    }
    if (absolute) {
        value = Operation(OperationCode::IAbsolute, NO_PRECISE, value);
    }
    if (negate) {
        value = Operation(OperationCode::INegate, NO_PRECISE, value);
    }
    return value;
}

Node ShaderIR::UnpackHalfImmediate(Instruction instr, bool has_negation) {
    const Node value = Immediate(instr.half_imm.PackImmediates());
    if (!has_negation) {
        return value;
    }
    const Node first_negate = GetPredicate(instr.half_imm.first_negate != 0);
    const Node second_negate = GetPredicate(instr.half_imm.second_negate != 0);

    return Operation(OperationCode::HNegate, NO_PRECISE, value, first_negate, second_negate);
}

Node ShaderIR::UnpackHalfFloat(Node value, Tegra::Shader::HalfType type) {
    return Operation(OperationCode::HUnpack, type, value);
}

Node ShaderIR::HalfMerge(Node dest, Node src, Tegra::Shader::HalfMerge merge) {
    switch (merge) {
    case Tegra::Shader::HalfMerge::H0_H1:
        return src;
    case Tegra::Shader::HalfMerge::F32:
        return Operation(OperationCode::HMergeF32, src);
    case Tegra::Shader::HalfMerge::Mrg_H0:
        return Operation(OperationCode::HMergeH0, dest, src);
    case Tegra::Shader::HalfMerge::Mrg_H1:
        return Operation(OperationCode::HMergeH1, dest, src);
    }
    UNREACHABLE();
    return src;
}

Node ShaderIR::GetOperandAbsNegHalf(Node value, bool absolute, bool negate) {
    if (absolute) {
        value = Operation(OperationCode::HAbsolute, NO_PRECISE, value);
    }
    if (negate) {
        value = Operation(OperationCode::HNegate, NO_PRECISE, value, GetPredicate(true),
                          GetPredicate(true));
    }
    return value;
}

Node ShaderIR::GetSaturatedHalfFloat(Node value, bool saturate) {
    if (!saturate) {
        return value;
    }
    const Node positive_zero = Immediate(std::copysignf(0, 1));
    const Node positive_one = Immediate(1.0f);
    return Operation(OperationCode::HClamp, NO_PRECISE, value, positive_zero, positive_one);
}

Node ShaderIR::GetPredicateComparisonFloat(PredCondition condition, Node op_a, Node op_b) {
    const std::unordered_map<PredCondition, OperationCode> PredicateComparisonTable = {
        {PredCondition::LessThan, OperationCode::LogicalFLessThan},
        {PredCondition::Equal, OperationCode::LogicalFEqual},
        {PredCondition::LessEqual, OperationCode::LogicalFLessEqual},
        {PredCondition::GreaterThan, OperationCode::LogicalFGreaterThan},
        {PredCondition::NotEqual, OperationCode::LogicalFNotEqual},
        {PredCondition::GreaterEqual, OperationCode::LogicalFGreaterEqual},
        {PredCondition::LessThanWithNan, OperationCode::LogicalFLessThan},
        {PredCondition::NotEqualWithNan, OperationCode::LogicalFNotEqual},
        {PredCondition::LessEqualWithNan, OperationCode::LogicalFLessEqual},
        {PredCondition::GreaterThanWithNan, OperationCode::LogicalFGreaterThan},
        {PredCondition::GreaterEqualWithNan, OperationCode::LogicalFGreaterEqual}};

    const auto comparison{PredicateComparisonTable.find(condition)};
    UNIMPLEMENTED_IF_MSG(comparison == PredicateComparisonTable.end(),
                         "Unknown predicate comparison operation");

    Node predicate = Operation(comparison->second, NO_PRECISE, op_a, op_b);

    if (condition == PredCondition::LessThanWithNan ||
        condition == PredCondition::NotEqualWithNan ||
        condition == PredCondition::LessEqualWithNan ||
        condition == PredCondition::GreaterThanWithNan ||
        condition == PredCondition::GreaterEqualWithNan) {

        predicate = Operation(OperationCode::LogicalOr, predicate,
                              Operation(OperationCode::LogicalFIsNan, op_a));
        predicate = Operation(OperationCode::LogicalOr, predicate,
                              Operation(OperationCode::LogicalFIsNan, op_b));
    }

    return predicate;
}

Node ShaderIR::GetPredicateComparisonInteger(PredCondition condition, bool is_signed, Node op_a,
                                             Node op_b) {
    const std::unordered_map<PredCondition, OperationCode> PredicateComparisonTable = {
        {PredCondition::LessThan, OperationCode::LogicalILessThan},
        {PredCondition::Equal, OperationCode::LogicalIEqual},
        {PredCondition::LessEqual, OperationCode::LogicalILessEqual},
        {PredCondition::GreaterThan, OperationCode::LogicalIGreaterThan},
        {PredCondition::NotEqual, OperationCode::LogicalINotEqual},
        {PredCondition::GreaterEqual, OperationCode::LogicalIGreaterEqual},
        {PredCondition::LessThanWithNan, OperationCode::LogicalILessThan},
        {PredCondition::NotEqualWithNan, OperationCode::LogicalINotEqual},
        {PredCondition::LessEqualWithNan, OperationCode::LogicalILessEqual},
        {PredCondition::GreaterThanWithNan, OperationCode::LogicalIGreaterThan},
        {PredCondition::GreaterEqualWithNan, OperationCode::LogicalIGreaterEqual}};

    const auto comparison{PredicateComparisonTable.find(condition)};
    UNIMPLEMENTED_IF_MSG(comparison == PredicateComparisonTable.end(),
                         "Unknown predicate comparison operation");

    Node predicate = SignedOperation(comparison->second, is_signed, NO_PRECISE, op_a, op_b);

    UNIMPLEMENTED_IF_MSG(condition == PredCondition::LessThanWithNan ||
                             condition == PredCondition::NotEqualWithNan ||
                             condition == PredCondition::LessEqualWithNan ||
                             condition == PredCondition::GreaterThanWithNan ||
                             condition == PredCondition::GreaterEqualWithNan,
                         "NaN comparisons for integers are not implemented");
    return predicate;
}

Node ShaderIR::GetPredicateComparisonHalf(Tegra::Shader::PredCondition condition, Node op_a,
                                          Node op_b) {
    const std::unordered_map<PredCondition, OperationCode> PredicateComparisonTable = {
        {PredCondition::LessThan, OperationCode::Logical2HLessThan},
        {PredCondition::Equal, OperationCode::Logical2HEqual},
        {PredCondition::LessEqual, OperationCode::Logical2HLessEqual},
        {PredCondition::GreaterThan, OperationCode::Logical2HGreaterThan},
        {PredCondition::NotEqual, OperationCode::Logical2HNotEqual},
        {PredCondition::GreaterEqual, OperationCode::Logical2HGreaterEqual},
        {PredCondition::LessThanWithNan, OperationCode::Logical2HLessThanWithNan},
        {PredCondition::NotEqualWithNan, OperationCode::Logical2HNotEqualWithNan},
        {PredCondition::LessEqualWithNan, OperationCode::Logical2HLessEqualWithNan},
        {PredCondition::GreaterThanWithNan, OperationCode::Logical2HGreaterThanWithNan},
        {PredCondition::GreaterEqualWithNan, OperationCode::Logical2HGreaterEqualWithNan}};

    const auto comparison{PredicateComparisonTable.find(condition)};
    UNIMPLEMENTED_IF_MSG(comparison == PredicateComparisonTable.end(),
                         "Unknown predicate comparison operation");

    const Node predicate = Operation(comparison->second, NO_PRECISE, op_a, op_b);

    return predicate;
}

OperationCode ShaderIR::GetPredicateCombiner(PredOperation operation) {
    const std::unordered_map<PredOperation, OperationCode> PredicateOperationTable = {
        {PredOperation::And, OperationCode::LogicalAnd},
        {PredOperation::Or, OperationCode::LogicalOr},
        {PredOperation::Xor, OperationCode::LogicalXor},
    };

    const auto op = PredicateOperationTable.find(operation);
    UNIMPLEMENTED_IF_MSG(op == PredicateOperationTable.end(), "Unknown predicate operation");
    return op->second;
}

Node ShaderIR::GetConditionCode(Tegra::Shader::ConditionCode cc) {
    switch (cc) {
    case Tegra::Shader::ConditionCode::NEU:
        return GetInternalFlag(InternalFlag::Zero, true);
    default:
        UNIMPLEMENTED_MSG("Unimplemented condition code: {}", static_cast<u32>(cc));
        return GetPredicate(static_cast<u64>(Pred::NeverExecute));
    }
}

void ShaderIR::SetRegister(NodeBlock& bb, Register dest, Node src) {
    bb.push_back(Operation(OperationCode::Assign, GetRegister(dest), src));
}

void ShaderIR::SetPredicate(NodeBlock& bb, u64 dest, Node src) {
    bb.push_back(Operation(OperationCode::LogicalAssign, GetPredicate(dest), src));
}

void ShaderIR::SetInternalFlag(NodeBlock& bb, InternalFlag flag, Node value) {
    bb.push_back(Operation(OperationCode::LogicalAssign, GetInternalFlag(flag), value));
}

void ShaderIR::SetLocalMemory(NodeBlock& bb, Node address, Node value) {
    bb.push_back(Operation(OperationCode::Assign, GetLocalMemory(address), value));
}

void ShaderIR::SetTemporal(NodeBlock& bb, u32 id, Node value) {
    SetRegister(bb, Register::ZeroIndex + 1 + id, value);
}

void ShaderIR::SetInternalFlagsFromFloat(NodeBlock& bb, Node value, bool sets_cc) {
    if (!sets_cc) {
        return;
    }
    const Node zerop = Operation(OperationCode::LogicalFEqual, value, Immediate(0.0f));
    SetInternalFlag(bb, InternalFlag::Zero, zerop);
    LOG_WARNING(HW_GPU, "Condition codes implementation is incomplete");
}

void ShaderIR::SetInternalFlagsFromInteger(NodeBlock& bb, Node value, bool sets_cc) {
    if (!sets_cc) {
        return;
    }
    const Node zerop = Operation(OperationCode::LogicalIEqual, value, Immediate(0));
    SetInternalFlag(bb, InternalFlag::Zero, zerop);
    LOG_WARNING(HW_GPU, "Condition codes implementation is incomplete");
}

Node ShaderIR::BitfieldExtract(Node value, u32 offset, u32 bits) {
    return Operation(OperationCode::UBitfieldExtract, NO_PRECISE, value, Immediate(offset),
                     Immediate(bits));
}

} // namespace VideoCommon::Shader
