// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "rosidl_typesupport_introspection_cpp/field_types.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// NOLINTNEXTLINE
namespace rosidl_typesupport_introspection_cpp
{

class MessageMembers;

class MessageMember
{
public:
    MessageMember( uint8_t primitiveType )
        : is_array_( false )
        , array_size_( 0 )
        , is_upper_bound_( false )
        , type_id_( primitiveType )
        , members_( nullptr )
    {
    }
    MessageMember() = default;
    bool is_array_;
    size_t array_size_;
    bool is_upper_bound_;
    uint8_t type_id_;
    MessageMembers *members_;
};

class MessageMembers
{
public:
    size_t member_count_;
    std::vector<MessageMember> members_;
    void *data;
};

} // namespace rosidl_typesupport_introspection_cpp
