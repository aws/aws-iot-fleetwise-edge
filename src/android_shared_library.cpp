// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws/iotfleetwise/ConsoleLogger.h"
#include "aws/iotfleetwise/IoTFleetWiseEngine.h"
#include "aws/iotfleetwise/IoTFleetWiseVersion.h"
#include "aws/iotfleetwise/LogLevel.h"
#include "aws/iotfleetwise/SignalTypes.h"
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <android/log.h>
#include <boost/filesystem.hpp>
#include <cstdlib>
#include <exception>
#include <jni.h>
#include <string>
#include <utility>
#include <vector>

#define LOG_TAG "FWE"
#define LOGE( x ) __android_log_print( ANDROID_LOG_ERROR, LOG_TAG, "%s", ( x ).c_str() )
#define LOGW( x ) __android_log_print( ANDROID_LOG_WARN, LOG_TAG, "%s", ( x ).c_str() )
#define LOGI( x ) __android_log_print( ANDROID_LOG_INFO, LOG_TAG, "%s", ( x ).c_str() )
#define LOGD( x ) __android_log_print( ANDROID_LOG_DEBUG, LOG_TAG, "%s", ( x ).c_str() )

static std::atomic<bool> mExit;
static std::shared_ptr<Aws::IoTFleetWise::IoTFleetWiseEngine> mEngine;

static void
printVersion()
{
    LOGI( std::string( "Version: " ) + FWE_VERSION_PROJECT_VERSION + ", git tag: " + FWE_VERSION_GIT_TAG +
          ", git commit sha: " + FWE_VERSION_GIT_COMMIT_SHA + ", Build time: " + FWE_VERSION_BUILD_TIME );
}

static void
configureLogging( const Json::Value &config )
{
    auto logLevel = Aws::IoTFleetWise::LogLevel::Trace;
    stringToLogLevel( config["staticConfig"]["internalParameters"]["systemWideLogLevel"].asString(), logLevel );
    Aws::IoTFleetWise::gSystemWideLogLevel = logLevel;
    Aws::IoTFleetWise::gLogColorOption = Aws::IoTFleetWise::LogColorOption::No;
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
        mqttConnection["iotFleetWiseTopicPrefix"] = mqttTopicPrefix;
        // Set system wide log level
        configureLogging( config );

        mEngine = std::make_shared<Aws::IoTFleetWise::IoTFleetWiseEngine>();
        // Connect the Engine
        if ( mEngine->connect( config, boost::filesystem::path( "" ) ) && mEngine->start() )
        {
            LOGI( std::string( "Started successfully" ) );
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
            LOGI( std::string( "Stopped successfully" ) );
            mEngine = nullptr;
            return EXIT_SUCCESS;
        }

        LOGE( std::string( "Stopped with errors" ) );
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
    if ( ( mEngine == nullptr ) || ( mEngine->mExternalGpsSource == nullptr ) )
    {
        return;
    }
    mEngine->mExternalGpsSource->setLocation( latitude, longitude );
}
#endif

#ifdef FWE_FEATURE_AAOS_VHAL
extern "C" JNIEXPORT jobjectArray JNICALL
Java_com_aws_iotfleetwise_Fwe_getVehiclePropertyInfo( JNIEnv *env, jobject me )
{
    static_cast<void>( me );
    jclass cls = env->FindClass( "[I" );
    jintArray iniVal = env->NewIntArray( 4 );
    jobjectArray outer;
    if ( ( mEngine == nullptr ) || ( mEngine->mAaosVhalSource == nullptr ) )
    {
        outer = env->NewObjectArray( 0, cls, iniVal );
    }
    else
    {
        auto propertyInfo = mEngine->mAaosVhalSource->getVehiclePropertyInfo();
        outer = env->NewObjectArray( static_cast<jsize>( propertyInfo.size() ), cls, iniVal );
        for ( size_t i = 0; i < propertyInfo.size(); i++ )
        {
            jintArray inner = env->NewIntArray( 4 );
            for ( size_t j = 0; j < 4; j++ )
            {
                jint val = static_cast<jint>( propertyInfo[i][j] );
                env->SetIntArrayRegion( inner, static_cast<jsize>( j ), 1, &val );
            }
            env->SetObjectArrayElement( outer, static_cast<jsize>( i ), inner );
            env->DeleteLocalRef( inner );
        }
    }
    return outer;
}

extern "C" JNIEXPORT void JNICALL
Java_com_aws_iotfleetwise_Fwe_setVehicleProperty( JNIEnv *env, jobject me, jint signalId, jobject value )
{
    static_cast<void>( env );
    static_cast<void>( me );
    if ( ( mEngine == nullptr ) || ( mEngine->mAaosVhalSource == nullptr ) )
    {
        return;
    }

    jclass doubleJClass = env->FindClass( "java/lang/Double" );
    jclass longJClass = env->FindClass( "java/lang/Long" );

    if ( env->IsInstanceOf( value, doubleJClass ) )
    {
        jmethodID doubleJMethodIdDoubleValue = env->GetMethodID( doubleJClass, "doubleValue", "()D" );
        jdouble doubleValue = env->CallDoubleMethod( value, doubleJMethodIdDoubleValue );
        mEngine->mAaosVhalSource->setVehicleProperty(
            static_cast<uint32_t>( signalId ),
            Aws::IoTFleetWise::DecodedSignalValue( doubleValue, Aws::IoTFleetWise::SignalType::DOUBLE ) );
    }
    else if ( env->IsInstanceOf( value, longJClass ) )
    {
        jmethodID longJMethodIdLongValue = env->GetMethodID( longJClass, "longValue", "()J" );
        jlong longValue = env->CallLongMethod( value, longJMethodIdLongValue );
        mEngine->mAaosVhalSource->setVehicleProperty(
            static_cast<uint32_t>( signalId ),
            Aws::IoTFleetWise::DecodedSignalValue( longValue, Aws::IoTFleetWise::SignalType::INT64 ) );
    }
    else
    {
        LOGE( std::string( "Unsupported value type" ) );
    }
}
#endif

extern "C" JNIEXPORT jintArray JNICALL
Java_com_aws_iotfleetwise_Fwe_getObdPidsToRequest( JNIEnv *env, jobject me )
{
    static_cast<void>( me );
    std::vector<uint8_t> requests;
    if ( ( mEngine != nullptr ) && ( mEngine->mOBDOverCANModule != nullptr ) )
    {
        requests = mEngine->mOBDOverCANModule->getExternalPIDsToRequest();
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
    if ( ( mEngine == nullptr ) || ( mEngine->mOBDOverCANModule == nullptr ) )
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
    mEngine->mOBDOverCANModule->setExternalPIDResponse( static_cast<uint8_t>( pid ), response );
}

extern "C" JNIEXPORT void JNICALL
Java_com_aws_iotfleetwise_Fwe_ingestCanMessage(
    JNIEnv *env, jobject me, jstring interfaceIdJString, jlong timestamp, jint messageId, jbyteArray dataJArray )
{
    static_cast<void>( me );
    if ( ( mEngine == nullptr ) || ( mEngine->mExternalCANDataSource == nullptr ) )
    {
        return;
    }
    auto interfaceIdCString = env->GetStringUTFChars( interfaceIdJString, 0 );
    Aws::IoTFleetWise::InterfaceID interfaceId( interfaceIdCString );
    env->ReleaseStringUTFChars( interfaceIdJString, interfaceIdCString );
    auto len = env->GetArrayLength( dataJArray );
    std::vector<uint8_t> data( static_cast<size_t>( len ) );
    env->GetByteArrayRegion( dataJArray, 0, len, (int8_t *)data.data() );
    mEngine->mExternalCANDataSource->ingestMessage(
        interfaceId, static_cast<Aws::IoTFleetWise::Timestamp>( timestamp ), static_cast<uint32_t>( messageId ), data );
}

extern "C" JNIEXPORT void JNICALL
Java_com_aws_iotfleetwise_Fwe_ingestSignalValueByName(
    JNIEnv *env, jobject me, jlong timestamp, jstring nameJString, jobject value )
{
    static_cast<void>( me );
    if ( ( mEngine == nullptr ) || ( mEngine->mNamedSignalDataSource == nullptr ) )
    {
        return;
    }

    auto nameCString = env->GetStringUTFChars( nameJString, 0 );
    std::string name( nameCString );
    env->ReleaseStringUTFChars( nameJString, nameCString );
    jclass doubleJClass = env->FindClass( "java/lang/Double" );
    jclass longJClass = env->FindClass( "java/lang/Long" );

    if ( env->IsInstanceOf( value, doubleJClass ) )
    {
        jmethodID doubleJMethodIdDoubleValue = env->GetMethodID( doubleJClass, "doubleValue", "()D" );
        jdouble doubleValue = env->CallDoubleMethod( value, doubleJMethodIdDoubleValue );
        mEngine->mNamedSignalDataSource->ingestSignalValue(
            static_cast<Aws::IoTFleetWise::Timestamp>( timestamp ),
            name,
            Aws::IoTFleetWise::DecodedSignalValue( doubleValue, Aws::IoTFleetWise::SignalType::DOUBLE ) );
    }
    else if ( env->IsInstanceOf( value, longJClass ) )
    {
        jmethodID longJMethodIdLongValue = env->GetMethodID( longJClass, "longValue", "()J" );
        jlong longValue = env->CallLongMethod( value, longJMethodIdLongValue );
        mEngine->mNamedSignalDataSource->ingestSignalValue(
            static_cast<Aws::IoTFleetWise::Timestamp>( timestamp ),
            name,
            Aws::IoTFleetWise::DecodedSignalValue( longValue, Aws::IoTFleetWise::SignalType::INT64 ) );
    }
    else
    {
        LOGE( std::string( "Unsupported value type" ) );
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_aws_iotfleetwise_Fwe_ingestMultipleSignalValuesByName( JNIEnv *env,
                                                                jobject me,
                                                                jlong timestamp,
                                                                jobject valuesJObject )
{
    static_cast<void>( me );
    if ( ( mEngine == nullptr ) || ( mEngine->mNamedSignalDataSource == nullptr ) )
    {
        return;
    }

    jclass valuesJClass = env->GetObjectClass( valuesJObject );
    jmethodID valuesJMethodIdEntrySet = env->GetMethodID( valuesJClass, "entrySet", "()Ljava/util/Set;" );
    jclass setJClass = env->FindClass( "java/util/Set" );
    jmethodID setJMethodIdIterator = env->GetMethodID( setJClass, "iterator", "()Ljava/util/Iterator;" );
    jclass iteratorJClass = env->FindClass( "java/util/Iterator" );
    jmethodID iteratorJMethodIdHasNext = env->GetMethodID( iteratorJClass, "hasNext", "()Z" );
    jmethodID iteratorJMethodIdNext = env->GetMethodID( iteratorJClass, "next", "()Ljava/lang/Object;" );
    jclass mapEntryJClass = env->FindClass( "java/util/Map$Entry" );
    jmethodID mapEntryJMethodIdGetKey = env->GetMethodID( mapEntryJClass, "getKey", "()Ljava/lang/Object;" );
    jmethodID mapEntryJMethodIdGetValue = env->GetMethodID( mapEntryJClass, "getValue", "()Ljava/lang/Object;" );
    jclass stringJClass = env->FindClass( "java/lang/String" );
    jmethodID stringJMethodIdToString = env->GetMethodID( stringJClass, "toString", "()Ljava/lang/String;" );
    jclass doubleJClass = env->FindClass( "java/lang/Double" );
    jmethodID doubleJMethodIdDoubleValue = env->GetMethodID( doubleJClass, "doubleValue", "()D" );
    jclass longJClass = env->FindClass( "java/lang/Long" );
    jmethodID longJMethodIdLongValue = env->GetMethodID( longJClass, "longValue", "()J" );

    jobject valuesEntrySetJObject = env->CallObjectMethod( valuesJObject, valuesJMethodIdEntrySet );
    jobject valuesEntrySetIteratorJObject = env->CallObjectMethod( valuesEntrySetJObject, setJMethodIdIterator );

    std::vector<std::pair<std::string, Aws::IoTFleetWise::DecodedSignalValue>> values;
    while ( env->CallBooleanMethod( valuesEntrySetIteratorJObject, iteratorJMethodIdHasNext ) )
    {
        jobject entryJObject = env->CallObjectMethod( valuesEntrySetIteratorJObject, iteratorJMethodIdNext );
        jobject nameJObject = env->CallObjectMethod( entryJObject, mapEntryJMethodIdGetKey );
        jobject valueJObject = env->CallObjectMethod( entryJObject, mapEntryJMethodIdGetValue );
        jstring nameJString = (jstring)env->CallObjectMethod( nameJObject, stringJMethodIdToString );
        auto nameCString = env->GetStringUTFChars( nameJString, 0 );
        std::string name( nameCString );
        if ( env->IsInstanceOf( valueJObject, doubleJClass ) )
        {
            jdouble value = env->CallDoubleMethod( valueJObject, doubleJMethodIdDoubleValue );
            values.emplace_back(
                name, Aws::IoTFleetWise::DecodedSignalValue( value, Aws::IoTFleetWise::SignalType::DOUBLE ) );
        }
        else if ( env->IsInstanceOf( valueJObject, longJClass ) )
        {
            jlong value = env->CallLongMethod( valueJObject, longJMethodIdLongValue );
            values.emplace_back( name,
                                 Aws::IoTFleetWise::DecodedSignalValue( value, Aws::IoTFleetWise::SignalType::INT64 ) );
        }
        else
        {
            LOGE( std::string( "Unsupported value type" ) );
        }
        env->ReleaseStringUTFChars( nameJString, nameCString );
        env->DeleteLocalRef( nameJString );
        env->DeleteLocalRef( valueJObject );
        env->DeleteLocalRef( nameJObject );
        env->DeleteLocalRef( entryJObject );
    }
    env->DeleteLocalRef( valuesEntrySetIteratorJObject );
    env->DeleteLocalRef( valuesEntrySetJObject );

    mEngine->mNamedSignalDataSource->ingestMultipleSignalValues( static_cast<Aws::IoTFleetWise::Timestamp>( timestamp ),
                                                                 values );
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
    std::string version = std::string( "v" ) + FWE_VERSION_PROJECT_VERSION;
    return env->NewStringUTF( version.c_str() );
}
