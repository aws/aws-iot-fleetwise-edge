// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "rcpputils/shared_library.hpp"
#include "rosidl_typesupport_introspection_cpp/message_type_support_decl.hpp"
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <memory>

namespace rclcpp
{
enum class CallbackGroupType
{
    Reentrant
};

// NOLINTNEXTLINE
class rcl_serialized_message_t
{
public:
    char *buffer;
    size_t buffer_length;
};

class GenericSubscription
{
public:                                       // NOLINT
    virtual ~GenericSubscription() = default; // NOLINT
    using SharedPtr = std::shared_ptr<GenericSubscription>;
};

class SerializedMessage
{
public:
    rcl_serialized_message_t &
    get_rcl_serialized_message()
    {
        return message;
    }
    rcl_serialized_message_t message;
};

class CallbackGroup
{
public:
    using SharedPtr = std::shared_ptr<CallbackGroup>;
};
class SubscriptionOptions
{
public:
    CallbackGroup::SharedPtr callback_group;
};
using QoS = size_t;

class NodeMock
{
public:
    // NOLINTNEXTLINE
    MOCK_METHOD( GenericSubscription::SharedPtr,
                 create_generic_subscription,
                 ( const std::string &topic_name,
                   const std::string &topic_type,
                   const rclcpp::QoS &qos,
                   std::function<void( std::shared_ptr<rclcpp::SerializedMessage> )> callback,
                   const SubscriptionOptions &options ) );

    // NOLINTNEXTLINE
    MOCK_METHOD( (std::map<std::string, std::vector<std::string>>), get_topic_names_and_types, () );
};

extern NodeMock *nodeMock;

class Node
{
public:
    Node( std::string name )
    {
        static_cast<void>( name ); // UNUSED
    }
    virtual ~Node() = default;

    // NOLINTNEXTLINE
    CallbackGroup::SharedPtr
    create_callback_group( CallbackGroupType type ) // NOLINT
    {
        static_cast<void>( type );
        return std::make_shared<CallbackGroup>();
    }

    GenericSubscription::SharedPtr
    // NOLINTNEXTLINE
    create_generic_subscription( const std::string &topic_name,
                                 const std::string &topic_type,
                                 const rclcpp::QoS &qos,
                                 std::function<void( std::shared_ptr<rclcpp::SerializedMessage> )> callback,
                                 const SubscriptionOptions &options )
    {
        return nodeMock->create_generic_subscription( topic_name, topic_type, qos, callback, options );
    };
    // NOLINTNEXTLINE
    std::map<std::string, std::vector<std::string>>
    get_topic_names_and_types() // NOLINT
    {
        return nodeMock->get_topic_names_and_types();
    };
    // NOLINTNEXTLINE
    const char *
    get_name() // NOLINT
    {
        return "MockNodeName";
    };
    // NOLINTNEXTLINE
    const char *
    get_namespace() // NOLINT
    {
        return "MockNodeNameSpace";
    };
};
class ExecutorOptions
{
};

class MultiThreadedExecutorMock
{
public:
    // NOLINTNEXTLINE
    MOCK_METHOD( void, spin, () );
};

extern MultiThreadedExecutorMock *multiThreadedExecutorMock;
// NOLINTNEXTLINE
namespace executors
{
class MultiThreadedExecutor
{
public:
    MultiThreadedExecutor( const rclcpp::ExecutorOptions &options = rclcpp::ExecutorOptions(),
                           size_t number_of_threads = 0,
                           bool yield_before_execute = false,
                           std::chrono::nanoseconds timeout = std::chrono::nanoseconds( -1 ) )
    {
        static_cast<void>( options );              // UNUSED
        static_cast<void>( number_of_threads );    // UNUSED
        static_cast<void>( yield_before_execute ); // UNUSED
        static_cast<void>( timeout );              // UNUSED
    }
    // NOLINTNEXTLINE
    void
    add_node( std::shared_ptr<rclcpp::Node> node_ptr ) // NOLINT
    {
        static_cast<void>( node_ptr ); // UNUSED
    }
    void
    cancel()
    {
    }

    // NOLINTNEXTLINE
    void
    spin() // NOLINT
    {
        multiThreadedExecutorMock->spin();
    }
};
} // namespace executors

class TypeSupportMock
{
public:
    // NOLINTNEXTLINE
    MOCK_METHOD( std::shared_ptr<rcpputils::SharedLibrary>,
                 get_typesupport_library,
                 ( const std::string &type, const std::string &typesupport_identifier ) );
    // NOLINTNEXTLINE
    MOCK_METHOD( const rosidl_message_type_support_t *,
                 get_typesupport_handle,
                 ( const std::string &type,
                   const std::string &typesupport_identifier,
                   rcpputils::SharedLibrary &library ) );
};

extern TypeSupportMock *typeSupportMock;

// NOLINTNEXTLINE
inline std::shared_ptr<rcpputils::SharedLibrary>
get_typesupport_library( const std::string &type, const std::string &typesupport_identifier )
{
    return typeSupportMock->get_typesupport_library( type, typesupport_identifier );
}

// NOLINTNEXTLINE
inline const rosidl_message_type_support_t *
get_typesupport_handle( const std::string &type,
                        const std::string &typesupport_identifier,
                        rcpputils::SharedLibrary &library )
{
    return typeSupportMock->get_typesupport_handle( type, typesupport_identifier, library );
}

inline std::string
expand_topic_or_service_name( const std::string &name,
                              const std::string &node_name,
                              const std::string &namespace_,
                              bool is_service = false )
{
    static_cast<void>( node_name );  // UNUSED
    static_cast<void>( namespace_ ); // UNUSED
    static_cast<void>( is_service ); // UNUSED
    return std::string( "EXPANDED__" + name );
}

} // namespace rclcpp
