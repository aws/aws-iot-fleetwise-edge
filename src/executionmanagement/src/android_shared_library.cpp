// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

// Includes
#include "ConsoleLogger.h"
#include "IoTFleetWiseEngine.h"
#include "IoTFleetWiseVersion.h"
#include "LogLevel.h"
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <android/log.h>
#include <cstdlib>
#include <exception>
#include <jni.h>
#include <string>

#define LOG_TAG "FWE"
#define LOGE( x ) __android_log_print( ANDROID_LOG_ERROR, LOG_TAG, "%s", ( x ).c_str() )
#define LOGW( x ) __android_log_print( ANDROID_LOG_WARN, LOG_TAG, "%s", ( x ).c_str() )
#define LOGI( x ) __android_log_print( ANDROID_LOG_INFO, LOG_TAG, "%s", ( x ).c_str() )
#define LOGD( x ) __android_log_print( ANDROID_LOG_DEBUG, LOG_TAG, "%s", ( x ).c_str() )

using namespace Aws::IoTFleetWise::ExecutionManagement;

static std::atomic<bool> mExit;
static std::shared_ptr<IoTFleetWiseEngine> mEngine;

static void
printVersion()
{
    LOGI( std::string( "Version: " ) + VERSION_PROJECT_VERSION + ", git tag: " + VERSION_GIT_TAG +
          ", git commit sha: " + VERSION_GIT_COMMIT_SHA + ", Build time: " + VERSION_BUILD_TIME );
}

static void
configureLogging( const Json::Value &config )
{
    Aws::IoTFleetWise::Platform::Linux::LogLevel logLevel = Aws::IoTFleetWise::Platform::Linux::LogLevel::Trace;
    stringToLogLevel( config["staticConfig"]["internalParameters"]["systemWideLogLevel"].asString(), logLevel );
    gSystemWideLogLevel = logLevel;
    gLogColorOption = Aws::IoTFleetWise::Platform::Linux::LogColorOption::No;
}

static std::string
readAssetFile( JNIEnv *env, jobject assetManager, std::string filename )
{
    AAsset *asset = AAssetManager_open( AAssetManager_fromJava( env, assetManager ), filename.c_str(), 0 );
    if ( !asset )
    {
        throw std::runtime_error( "could not open " + filename );
    }
    std::string content;
    for ( ;; )
    {
        char buf[1024];
        auto sizeRead = AAsset_read( asset, buf, sizeof( buf ) - 1 );
        if ( sizeRead < 0 )
        {
            AAsset_close( asset );
            throw std::runtime_error( "error reading from " + filename );
        }
        if ( sizeRead == 0 )
        {
            break;
        }
        buf[sizeRead] = '\0';
        content += buf;
    }
    AAsset_close( asset );
    return content;
}

extern "C" JNIEXPORT jint JNICALL
Java_com_aws_iotfleetwise_Fwe_run( JNIEnv *env,
                                   jobject me,
                                   jobject assetManager,
                                   jstring vehicleNameJString,
                                   jstring endpointUrlJString,
                                   jstring certificateJString,
                                   jstring privateKeyJString,
                                   jstring mqttTopicPrefixJString )
{
    static_cast<void>( me );
    try
    {
        auto vehicleNameCString = env->GetStringUTFChars( vehicleNameJString, 0 );
        std::string vehicleName( vehicleNameCString );
        env->ReleaseStringUTFChars( vehicleNameJString, vehicleNameCString );
        auto endpointUrlCString = env->GetStringUTFChars( endpointUrlJString, 0 );
        std::string endpointUrl( endpointUrlCString );
        env->ReleaseStringUTFChars( endpointUrlJString, endpointUrlCString );
        auto certificateCString = env->GetStringUTFChars( certificateJString, 0 );
        std::string certificate( certificateCString );
        env->ReleaseStringUTFChars( certificateJString, certificateCString );
        auto privateKeyCString = env->GetStringUTFChars( privateKeyJString, 0 );
        std::string privateKey( privateKeyCString );
        env->ReleaseStringUTFChars( privateKeyJString, privateKeyCString );
        auto mqttTopicPrefixCString = env->GetStringUTFChars( mqttTopicPrefixJString, 0 );
        std::string mqttTopicPrefix( mqttTopicPrefixCString );
        env->ReleaseStringUTFChars( mqttTopicPrefixJString, mqttTopicPrefixCString );
        if ( mqttTopicPrefix.empty() )
        {
            mqttTopicPrefix = "$aws/iotfleetwise/";
        }

        printVersion();
        LOGI( "vehicleName: " + vehicleName + ", endpointUrl: " + endpointUrl +
              ", mqttTopicPrefix: " + mqttTopicPrefix );

        std::string configFilename = "config-0.json";
        auto configJson = readAssetFile( env, assetManager, configFilename );
        Json::Value config;
        try
        {
            std::stringstream configFileStream( configJson );
            configFileStream >> config;
        }
        catch ( ... )
        {
            LOGE( std::string( "Failed to parse: " ) + configFilename );
            return EXIT_FAILURE;
        }
        auto rootCA = readAssetFile( env, assetManager, "AmazonRootCA1.pem" );
        auto &mqttConnection = config["staticConfig"]["mqttConnection"];
        mqttConnection["endpointUrl"] = endpointUrl;
        mqttConnection["privateKey"] = privateKey;
        mqttConnection["certificate"] = certificate;
        mqttConnection["rootCA"] = rootCA;
        mqttConnection["clientId"] = vehicleName;
        mqttTopicPrefix += "vehicles/" + vehicleName;
        mqttConnection["collectionSchemeListTopic"] = mqttTopicPrefix + "/collection_schemes";
        mqttConnection["decoderManifestTopic"] = mqttTopicPrefix + "/decoder_manifests";
        mqttConnection["canDataTopic"] = mqttTopicPrefix + "/signals";
        mqttConnection["checkinTopic"] = mqttTopicPrefix + "/checkins";
        // Set system wide log level
        configureLogging( config );

        mEngine = std::make_shared<IoTFleetWiseEngine>();
        // Connect the Engine
        if ( mEngine->connect( config ) && mEngine->start() )
        {
            LOGI( std::string( " AWS IoT FleetWise Edge Service Started successfully " ) );
        }
        else
        {
            mEngine = nullptr;
            return EXIT_FAILURE;
        }

        mExit = false;
        while ( !mExit )
        {
            sleep( 1 );
        }
        if ( mEngine->stop() && mEngine->disconnect() )
        {
            LOGI( std::string( " AWS IoT FleetWise Edge Service Stopped successfully " ) );
            mEngine = nullptr;
            return EXIT_SUCCESS;
        }

        LOGE( std::string( " AWS IoT FleetWise Edge Service Stopped with errors " ) );
        mEngine = nullptr;
        return EXIT_FAILURE;
    }
    catch ( const std::exception &e )
    {
        LOGE( std::string( "Unhandled exception: " ) + std::string( e.what() ) );
        mEngine = nullptr;
        return EXIT_FAILURE;
    }
    catch ( ... )
    {
        LOGE( std::string( "Unknown exception" ) );
        mEngine = nullptr;
        return EXIT_FAILURE;
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_aws_iotfleetwise_Fwe_stop( JNIEnv *env, jobject me )
{
    static_cast<void>( env );
    static_cast<void>( me );
    mExit = true;
}

#ifdef FWE_FEATURE_EXTERNAL_GPS
extern "C" JNIEXPORT void JNICALL
Java_com_aws_iotfleetwise_Fwe_setLocation( JNIEnv *env, jobject me, jdouble latitude, jdouble longitude )
{
    static_cast<void>( env );
    static_cast<void>( me );
    if ( mEngine == nullptr )
    {
        return;
    }
    mEngine->setExternalGpsLocation( latitude, longitude );
}
#endif

extern "C" JNIEXPORT jintArray JNICALL
Java_com_aws_iotfleetwise_Fwe_getObdPidsToRequest( JNIEnv *env, jobject me )
{
    static_cast<void>( me );
    std::vector<uint8_t> requests;
    if ( mEngine != nullptr )
    {
        requests = mEngine->getExternalOBDPIDsToRequest();
    }
    jintArray ret = env->NewIntArray( static_cast<jsize>( requests.size() ) );
    for ( size_t i = 0; i < requests.size(); i++ )
    {
        jint pid = requests[i];
        env->SetIntArrayRegion( ret, static_cast<jsize>( i ), 1, &pid );
    }
    return ret;
}

extern "C" JNIEXPORT void JNICALL
Java_com_aws_iotfleetwise_Fwe_setObdPidResponse( JNIEnv *env, jobject me, jint pid, jintArray responseJArray )
{
    static_cast<void>( me );
    if ( mEngine == nullptr )
    {
        return;
    }
    auto len = env->GetArrayLength( responseJArray );
    std::vector<uint8_t> response( static_cast<size_t>( len ) );
    for ( jsize i = 0; i < len; i++ )
    {
        jint byte;
        env->GetIntArrayRegion( responseJArray, i, 1, &byte );
        response[static_cast<size_t>( i )] = static_cast<uint8_t>( byte );
    }
    mEngine->setExternalOBDPIDResponse( static_cast<uint8_t>( pid ), response );
}

extern "C" JNIEXPORT void JNICALL
Java_com_aws_iotfleetwise_Fwe_ingestCanMessage(
    JNIEnv *env, jobject me, jstring interfaceIdJString, jlong timestamp, jint messageId, jbyteArray dataJArray )
{
    static_cast<void>( me );
    if ( mEngine == nullptr )
    {
        return;
    }
    auto interfaceIdCString = env->GetStringUTFChars( interfaceIdJString, 0 );
    std::string interfaceId( interfaceIdCString );
    env->ReleaseStringUTFChars( interfaceIdJString, interfaceIdCString );
    auto len = env->GetArrayLength( dataJArray );
    std::vector<uint8_t> data( static_cast<size_t>( len ) );
    env->GetByteArrayRegion( dataJArray, 0, len, (int8_t *)data.data() );
    mEngine->ingestExternalCANMessage(
        interfaceId, static_cast<Timestamp>( timestamp ), static_cast<uint32_t>( messageId ), data );
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_aws_iotfleetwise_Fwe_getStatusSummary( JNIEnv *env, jobject me )
{
    static_cast<void>( me );
    std::string status;
    if ( mEngine != nullptr )
    {
        status = mEngine->getStatusSummary();
    }
    return env->NewStringUTF( status.c_str() );
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_aws_iotfleetwise_Fwe_getVersion( JNIEnv *env, jobject me )
{
    static_cast<void>( me );
    std::string version = std::string( "v" ) + VERSION_PROJECT_VERSION;
    return env->NewStringUTF( version.c_str() );
}
