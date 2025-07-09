#!/usr/bin/env python3
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

import re

import boto3


def get_latest_ami(region, image_filter):
    ec2_client = boto3.client("ec2", region_name=region)
    images = []
    next_token = {}
    while True:
        result = ec2_client.describe_images(
            Owners=["amazon"],
            Filters=[
                {
                    "Name": "name",
                    "Values": [image_filter],
                },
                {"Name": "state", "Values": ["available"]},
            ],
            **next_token,
        )
        if len(result["Images"]) == 0:
            raise Exception(f"No AMIs found for {image_filter} in {region}")
        images += result["Images"]
        if "NextToken" not in result:
            break
        next_token = {"NextToken": result["NextToken"]}
    images.sort(key=lambda x: x["CreationDate"], reverse=True)
    print(
        f"  Latest AMI for {image_filter} in {region}: "
        f"{images[0]['ImageId']} ({images[0]['CreationDate']})"
    )
    return images[0]["ImageId"]


def update_amis(cfn_filename, mapping, image_filter):
    print(f"Updating {cfn_filename} for {image_filter}...")
    output = ""
    mapping_found = False
    in_mapping = False
    ami_id_line = ""
    with open(cfn_filename) as fp:
        for line in fp:
            if line == f"  {mapping}:\n":
                mapping_found = True
                in_mapping = True
            elif in_mapping:
                region_match = re.match(r"^    ([a-z0-9-]+):$", line)
                if not region_match:
                    if ami_id_line:
                        line = ami_id_line
                        ami_id_line = ""
                    else:
                        in_mapping = False
                else:
                    region = region_match.group(1)
                    ami_id_line = "      AMIID: " + get_latest_ami(region, image_filter) + "\n"
            output += line
    if not mapping_found:
        raise Exception(f"Mapping {mapping} not found in {cfn_filename}")
    with open(cfn_filename, "w") as fp:
        fp.write(output)


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description="Updates AMIs in a CloudFormation template")
    parser.add_argument(
        "-f", "--file", metavar="FILE", required=True, help="CloudFormation YAML file"
    )
    parser.add_argument(
        "-m", "--mapping", metavar="MAPPING", required=True, help="CloudFormation Mapping"
    )
    parser.add_argument(
        "-i", "--image-filter", metavar="FILTER", required=True, help="Image filter"
    )
    args = parser.parse_args()
    update_amis(args.file, args.mapping, args.image_filter)
