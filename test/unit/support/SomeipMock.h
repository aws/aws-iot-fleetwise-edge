// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <gmock/gmock.h>
#include <vsomeip/vsomeip.hpp>

class SomeipApplicationMock : public vsomeip::application
{
public:
    MOCK_METHOD( const std::string &, get_name, (), ( const ) );
    MOCK_METHOD( vsomeip_v3::client_t, get_client, (), ( const ) );
    MOCK_METHOD( vsomeip_v3::diagnosis_t, get_diagnosis, (), ( const ) );
    MOCK_METHOD( bool, init, () );
    MOCK_METHOD( void, start, () );
    MOCK_METHOD( void, stop, () );
    MOCK_METHOD( void, process, ( int _number ) );
    MOCK_METHOD( vsomeip_v3::security_mode_e, get_security_mode, (), ( const ) );
    MOCK_METHOD( void,
                 stop_offer_service,
                 ( vsomeip_v3::service_t _service,
                   vsomeip_v3::instance_t _instance,
                   vsomeip_v3::major_version_t,
                   vsomeip_v3::minor_version_t ),
                 ( override ) );

    MOCK_METHOD( void,
                 offer_event,
                 ( vsomeip_v3::service_t _service,
                   vsomeip_v3::instance_t _instance,
                   vsomeip_v3::event_t,
                   const std::set<short unsigned int> &,
                   vsomeip_v3::event_type_e,
                   std::chrono::milliseconds,
                   bool,
                   bool,
                   const vsomeip_v3::epsilon_change_func_t &,
                   vsomeip_v3::reliability_type_e ),
                 ( override ) );

    MOCK_METHOD( void,
                 unregister_subscription_handler,
                 ( vsomeip_v3::service_t _service, vsomeip_v3::instance_t _instance, vsomeip_v3::eventgroup_t ),
                 ( override ) );
    MOCK_METHOD( void,
                 offer_service,
                 ( vsomeip_v3::service_t _service,
                   vsomeip_v3::instance_t _instance,
                   vsomeip_v3::major_version_t,
                   vsomeip_v3::minor_version_t ),
                 ( override ) );

    MOCK_METHOD( void,
                 stop_offer_event,
                 ( vsomeip_v3::service_t _service, vsomeip_v3::instance_t _instance, vsomeip_v3::event_t ),
                 ( override ) );

    MOCK_METHOD( void,
                 request_service,
                 ( vsomeip_v3::service_t _service,
                   vsomeip_v3::instance_t _instance,
                   vsomeip_v3::major_version_t,
                   vsomeip_v3::minor_version_t ),
                 ( override ) );

    MOCK_METHOD( void,
                 release_service,
                 ( vsomeip_v3::service_t _service, vsomeip_v3::instance_t _instance ),
                 ( override ) );

    MOCK_METHOD( void,
                 request_event,
                 ( vsomeip_v3::service_t _service,
                   vsomeip_v3::instance_t _instance,
                   vsomeip_v3::event_t,
                   const std::set<short unsigned int> &,
                   vsomeip_v3::event_type_e,
                   vsomeip_v3::reliability_type_e ),
                 ( override ) );

    MOCK_METHOD( void,
                 release_event,
                 ( vsomeip_v3::service_t _service, vsomeip_v3::instance_t _instance, vsomeip_v3::event_t ),
                 ( override ) );

    MOCK_METHOD( void,
                 subscribe,
                 ( vsomeip_v3::service_t _service,
                   vsomeip_v3::instance_t _instance,
                   vsomeip_v3::eventgroup_t,
                   vsomeip_v3::major_version_t,
                   vsomeip_v3::event_t ),
                 ( override ) );

    MOCK_METHOD( void,
                 unsubscribe,
                 ( vsomeip_v3::service_t _service, vsomeip_v3::instance_t _instance, vsomeip_v3::eventgroup_t ),
                 ( override ) );

    MOCK_METHOD( bool,
                 is_available,
                 ( vsomeip_v3::service_t _service,
                   vsomeip_v3::instance_t _instance,
                   vsomeip_v3::major_version_t,
                   vsomeip_v3::minor_version_t ),
                 ( const, override ) );

    MOCK_METHOD( bool,
                 are_available,
                 ( vsomeip::application::available_t &,
                   vsomeip::service_t,
                   vsomeip::instance_t,
                   vsomeip::major_version_t,
                   vsomeip::minor_version_t ),
                 ( const ) );

    MOCK_METHOD( void, send, (std::shared_ptr<vsomeip::message>));

    MOCK_METHOD( void,
                 notify,
                 (vsomeip::service_t, vsomeip::instance_t, vsomeip::event_t, std::shared_ptr<vsomeip::payload>, bool),
                 ( const ) );

    MOCK_METHOD( void, clear_all_handler, (), ( override ) );
    MOCK_METHOD( void,
                 register_message_handler,
                 (vsomeip::service_t, vsomeip::instance_t, vsomeip::method_t, const vsomeip::message_handler_t &),
                 ( override ) );
    MOCK_METHOD( void,
                 notify_one,
                 (vsomeip::service_t,
                  vsomeip::instance_t,
                  vsomeip::event_t,
                  std::shared_ptr<vsomeip::payload>,
                  vsomeip::client_t,
                  bool),
                 ( const ) );

    MOCK_METHOD( void,
                 register_availability_handler,
                 ( vsomeip::service_t,
                   vsomeip::instance_t,
                   const vsomeip::availability_handler_t &,
                   vsomeip::major_version_t,
                   vsomeip::minor_version_t ),
                 ( override ) );

    MOCK_METHOD(
        void,
        register_subscription_handler,
        (vsomeip::service_t, vsomeip::instance_t, vsomeip::eventgroup_t, const vsomeip::subscription_handler_t &),
        ( override ) );

    MOCK_METHOD(
        void,
        register_async_subscription_handler,
        (vsomeip::service_t, vsomeip::instance_t, vsomeip::eventgroup_t, const vsomeip::async_subscription_handler_t &),
        ( override ) );

    MOCK_METHOD( bool, is_routing, (), ( const override ) );

    MOCK_METHOD( void,
                 unsubscribe,
                 ( vsomeip::service_t, vsomeip::instance_t, vsomeip::eventgroup_t, vsomeip::event_t ),
                 ( override ) );

    MOCK_METHOD( void,
                 register_subscription_status_handler,
                 (vsomeip::service_t,
                  vsomeip::instance_t,
                  vsomeip::eventgroup_t,
                  vsomeip::event_t,
                  const vsomeip::subscription_status_handler_t &,
                  bool));

    MOCK_METHOD( void,
                 unregister_subscription_status_handler,
                 ( vsomeip::service_t, vsomeip::instance_t, vsomeip::eventgroup_t, vsomeip::event_t ),
                 ( override ) );

    MOCK_METHOD( void,
                 unregister_availability_handler,
                 ( vsomeip::service_t, vsomeip::instance_t, vsomeip::major_version_t, vsomeip::minor_version_t ),
                 ( override ) );

    MOCK_METHOD( void,
                 register_subscription_status_handler,
                 (vsomeip::service_t,
                  vsomeip::instance_t,
                  vsomeip::eventgroup_t,
                  vsomeip::event_t,
                  vsomeip::subscription_status_handler_t,
                  bool),
                 ( override ) );

    MOCK_METHOD( void,
                 subscribe_with_debounce,
                 (vsomeip::service_t,
                  vsomeip::instance_t,
                  vsomeip::eventgroup_t,
                  vsomeip::major_version_t,
                  vsomeip::event_t,
                  const vsomeip_v3::debounce_filter_t &),
                 ( override ) );

    MOCK_METHOD( void,
                 register_message_acceptance_handler,
                 ( const vsomeip::message_acceptance_handler_t &_handler ),
                 ( override ) );

    MOCK_METHOD( (std::map<std::string, std::string>), get_additional_data, (const std::string &), ( override ) );

    MOCK_METHOD( void,
                 register_availability_handler,
                 ( vsomeip::service_t,
                   vsomeip::instance_t,
                   const vsomeip::availability_state_handler_t &,
                   vsomeip::major_version_t,
                   vsomeip::minor_version_t ),
                 ( override ) );

    MOCK_METHOD(
        void,
        register_subscription_handler,
        (vsomeip::service_t, vsomeip::instance_t, vsomeip::eventgroup_t, const vsomeip::subscription_handler_sec_t &),
        ( override ) );

    MOCK_METHOD(
        void,
        register_async_subscription_handler,
        ( vsomeip::service_t, vsomeip::instance_t, vsomeip::eventgroup_t, vsomeip::async_subscription_handler_sec_t ),
        ( override ) );

    MOCK_METHOD( void,
                 set_sd_acceptance_required,
                 (const vsomeip::remote_info_t &, const std::string &, bool),
                 ( override ) );

    MOCK_METHOD( void, set_sd_acceptance_required, (const sd_acceptance_map_type_t &, bool), ( override ) );

    MOCK_METHOD( void, register_sd_acceptance_handler, (const vsomeip::sd_acceptance_handler_t &), ( override ) );

    MOCK_METHOD( void,
                 register_reboot_notification_handler,
                 (const vsomeip::reboot_notification_handler_t &),
                 ( override ) );

    MOCK_METHOD( void, register_routing_ready_handler, (const vsomeip::routing_ready_handler_t &), ( override ) );

    MOCK_METHOD( void, register_routing_state_handler, (const vsomeip::routing_state_handler_t &), ( override ) );

    MOCK_METHOD( bool,
                 update_service_configuration,
                 (vsomeip::service_t, vsomeip::instance_t, uint16_t, bool, bool, bool),
                 ( override ) );

    MOCK_METHOD( void,
                 update_security_policy_configuration,
                 (uint32_t,
                  uint32_t,
                  std::shared_ptr<vsomeip::policy>,
                  std::shared_ptr<vsomeip::payload>,
                  const vsomeip::security_update_handler_t &),
                 ( override ) );

    MOCK_METHOD( void,
                 remove_security_policy_configuration,
                 (uint32_t, uint32_t, const vsomeip::security_update_handler_t &),
                 ( override ) );

    MOCK_METHOD(
        void,
        register_subscription_handler,
        (vsomeip::service_t, vsomeip::instance_t, vsomeip::eventgroup_t, const vsomeip::subscription_handler_ext_t &),
        ( override ) );

    MOCK_METHOD( void,
                 register_async_subscription_handler,
                 (vsomeip::service_t,
                  vsomeip::instance_t,
                  vsomeip::eventgroup_t,
                  const vsomeip::async_subscription_handler_ext_t &),
                 ( override ) );

    MOCK_METHOD( void, register_state_handler, (const vsomeip::state_handler_t &));
    MOCK_METHOD( void, unregister_state_handler, () );
    MOCK_METHOD( void, unregister_message_handler, ( vsomeip::service_t, vsomeip::instance_t, vsomeip::method_t ) );

    MOCK_METHOD( bool, is_routing, () );
    MOCK_METHOD( void, set_routing_state, ( vsomeip::routing_state_e ) );
    MOCK_METHOD( void,
                 get_offered_services_async,
                 (vsomeip::offer_type_e, const vsomeip::offered_services_handler_t &));
    MOCK_METHOD( void, set_watchdog_handler, ( const vsomeip::watchdog_handler_t &, std::chrono::seconds ) );

    MOCK_METHOD( sd_acceptance_map_type_t, get_sd_acceptance_required, () );

    MOCK_METHOD( void,
                 register_message_handler_ext,
                 ( vsomeip::service_t,
                   vsomeip::instance_t,
                   vsomeip::method_t,
                   const vsomeip::message_handler_t &,
                   vsomeip::handler_registration_type_e ) );

    MOCK_METHOD( std::shared_ptr<vsomeip::configuration>, get_configuration, (), ( const ) );

    MOCK_METHOD( std::shared_ptr<vsomeip::policy_manager>, get_policy_manager, (), ( const ) );
};
