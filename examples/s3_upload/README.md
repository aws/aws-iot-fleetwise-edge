# S3 upload example

This example uses the AWS credentials provided to upload a configured file to S3.

This guide assumes you have already configured your development machine and built the examples using
[these](../README.md#building) instructions.

## Provisioning and Configuration

1. Run the following commands _on the development machine_ to create a new S3 bucket for storing the
   uploaded file, and an IAM role and IoT Credentials Provider role alias (via a CloudFormation
   stack) for FWE to access the S3 bucket.

   ```bash
   cd ~/aws-iot-fleetwise-edge \
   && REGION="us-east-1" \
   && UUID=`cat /proc/sys/kernel/random/uuid` \
   && DISAMBIGUATOR=${UUID:0:8} \
   && S3_BUCKET_NAME="s3-upload-demo-bucket-${DISAMBIGUATOR}" \
   && STACK_NAME="s3-upload-demo-credentials-provider-${DISAMBIGUATOR}" \
   && ROLE_ALIAS="s3-upload-demo-role-alias-${DISAMBIGUATOR}" \
   && ACCOUNT_ID=`aws sts get-caller-identity | jq -r .Account` \
   && echo "S3_BUCKET_NAME=${S3_BUCKET_NAME}" > tools/cloud/demo.env \
   && aws s3 mb s3://${S3_BUCKET_NAME} --region ${REGION} \
   && aws cloudformation create-stack \
       --region ${REGION} \
       --stack-name ${STACK_NAME} \
       --template-body file://tools/cfn-templates/iot-credentials-provider.yml \
       --parameters ParameterKey=RoleAlias,ParameterValue=${ROLE_ALIAS} \
           ParameterKey=S3BucketName,ParameterValue=${S3_BUCKET_NAME} \
           ParameterKey=IoTCoreRegion,ParameterValue=${REGION} \
           ParameterKey=S3BucketPrefixPattern,ParameterValue='"*"' \
           ParameterKey=S3Actions,ParameterValue='"s3:PutObject"' \
       --capabilities CAPABILITY_AUTO_EXPAND CAPABILITY_NAMED_IAM
   ```

1. Run the following to provision credentials for the vehicle:

   ```bash
   cd ~/aws-iot-fleetwise-edge/examples/s3_upload \
   && mkdir -p build_config \
   && ../../tools/provision.sh \
       --region us-east-1 \
       --vehicle-name fwe-example-s3-upload \
       --certificate-pem-outfile build_config/certificate.pem \
       --private-key-outfile build_config/private-key.key \
       --endpoint-url-outfile build_config/endpoint.txt \
       --vehicle-name-outfile build_config/vehicle-name.txt \
       --creds-role-alias ${ROLE_ALIAS} \
       --creds-role-alias-outfile build_config/creds-role-alias.txt \
       --creds-endpoint-url-outfile build_config/creds-endpoint.txt \
   && ../../tools/configure-fwe.sh \
       --input-config-file ../../configuration/static-config.json \
       --output-config-file build_config/config-0.json \
       --log-color Yes \
       --log-level Trace \
       --vehicle-name `cat build_config/vehicle-name.txt` \
       --endpoint-url `cat build_config/endpoint.txt` \
       --certificate-file `realpath build_config/certificate.pem` \
       --private-key-file `realpath build_config/private-key.key` \
       --persistency-path `realpath build_config` \
       --creds-role-alias `cat build_config/creds-role-alias.txt` \
       --creds-endpoint-url `cat build_config/creds-endpoint.txt` \
   && MY_S3_UPLOAD_CONFIG=`echo {} \
       | jq ".bucketName=\"${S3_BUCKET_NAME}\"" \
       | jq ".bucketRegion=\"us-east-1\"" \
       | jq ".bucketOwner=\"${ACCOUNT_ID}\"" \
       | jq ".maxConnections=2" \
       | jq ".localFilePath=\"/tmp/s3_upload_test.txt\"" \
       | jq ".remoteObjectKey=\"s3_upload_test.txt\""` \
   && OUTPUT_CONFIG=`jq ".myS3Upload=${MY_S3_UPLOAD_CONFIG}" build_config/config-0.json` \
   && echo "${OUTPUT_CONFIG}" > build_config/config-0.json
   ```

## Running the example

1. Create the file to be uploaded:

   ```bash
   echo "Hello world!" > /tmp/s3_upload_test.txt
   ```

1. Run the example with the config file:

   ```bash
   ../build/s3_upload/fwe-example-s3-upload \
       build_config/config-0.json
   ```

1. After 2 seconds the configured file is uploaded to the S3 bucket.
