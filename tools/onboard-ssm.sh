# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

# This script needs to be compatible with both bash (for S32G) and ash (for iWave). That is why
# we don't make the script executable nor define a shebang. It needs to be passed to the shell that
# is available in the device:
#
# bash ./onboard-ssm.sh
#
# or
#
# ash ./onboard-ssm.sh

set -euo pipefail

ARCH_X86="i386 i686"
ARCH_X86_64="x86_64"
ARCH_ARM64="aarch64 aarch64_be armv8b armv8l"
ARCH_ARM32="armv7l"
SUPPORTED_ARCHITECTURES="$ARCH_X86 $ARCH_X86_64 $ARCH_ARM64 $ARCH_ARM32"

REGISTRATION_PATH="/var/lib/amazon/ssm/registration"

EXIT_CODE_SSM_START_FAILED=1
EXIT_CODE_WRONG_ARGUMENTS=2
EXIT_CODE_WRONG_ARCH=3
EXIT_CODE_ALREADY_REGISTERED=4
EXIT_CODE_EXTRACT_FAILED=5

parse_args() {
  while [ "$#" -gt 0 ]; do
    case $1 in
    --code)
      readonly ACTIVATION_CODE=$2
      ;;
    --id)
      readonly ACTIVATION_ID=$2
      ;;
    --region)
      readonly REGION=$2
      ;;
    --override)
      readonly OVERRIDE="true"
      ;;
    --help)
      print_help "$@"
      exit 0
      ;;
    esac
    shift
  done
}

verify_args() {
  local parse_error=false

  if [ -z ${ACTIVATION_CODE+x} ]; then
    printf "Activation code not set!\n"
    parse_error=true
  fi

  if [ -z ${ACTIVATION_ID+x} ]; then
    printf "Activation ID not set!\n"
    parse_error=true
  fi

  if [ -z ${REGION+x} ]; then
    printf "Region not set!\n"
    parse_error=true
  fi

  if [ "$parse_error" = true ]; then
    print_help "$@"
    exit $EXIT_CODE_WRONG_ARGUMENTS
  fi
}

print_help() {
    printf "
Usage:

%s [--override] --code ACTIVATION_CODE --id ACTIVATION_ID --region REGION

where:
    --code          the SSM activation code
    --id            the SSM activation id
    --region        the AWS region to onboard the device to
    --override      force re-registration if device is already registered (optional)\n" "$(basename "$0")"
}

check_arch() {
  local ARCH
  ARCH=$(uname -m)

  for supported in $SUPPORTED_ARCHITECTURES; do
    if [ "$ARCH" = "$supported" ]; then
      return 0
    fi
  done

  printf >&2 "Machine is running unsupported architecture %s
Supported architectures: %s\n" "$ARCH" "$SUPPORTED_ARCHITECTURES"
  exit $EXIT_CODE_WRONG_ARCH
}

install_ssm() {
  printf "Installing the Amazon SSM agent ...\n"

  local TEMP_INSTALL_DIR
  TEMP_INSTALL_DIR=$(mktemp -d)
  trap "rm -rf '$TEMP_INSTALL_DIR'" EXIT
  ORG_DIR=`pwd`
  cd "$TEMP_INSTALL_DIR"

  local ARCH_ID
  ARCH_ID="$(get_ssm_arch_identifier)"

  curl -s --fail --show-error "https://s3.${REGION}.amazonaws.com/amazon-ssm-${REGION}/latest/debian_${ARCH_ID}/amazon-ssm-agent.deb" -o amazon-ssm-agent.deb

  # Install scripts require bash, so if it's not available, extract the package, change to sh and install:
  if command -v dpkg > /dev/null && ! ( command -v bash > /dev/null ); then
    mkdir tmp
    dpkg-deb -R amazon-ssm-agent.deb tmp
    sed -i 's/bash/sh/' tmp/DEBIAN/preinst tmp/DEBIAN/postinst tmp/DEBIAN/prerm
    sh tmp/DEBIAN/preinst
    cp -r tmp/etc tmp/lib tmp/usr /
    sh tmp/DEBIAN/postinst
  elif command -v dpkg > /dev/null; then
    dpkg -i "amazon-ssm-agent.deb"
  elif command -v ar > /dev/null; then
    ar x "amazon-ssm-agent.deb"
    tar -zxf control.tar.gz
    mkdir data
    tar -C data -zxf data.tar.gz
    bash ./preinst
    cp -r data/* /
    bash ./postinst
  else
    echo "Error: No method of extracting package found"
    exit ${EXIT_CODE_EXTRACT_FAILED}
  fi

  # create logger configuration
  cp /etc/amazon/ssm/seelog.xml.template /etc/amazon/ssm/seelog.xml
  printf "Successfully installed SSM agent!\n"
  cd "$ORG_DIR"
}

# map cpu arch to ssm arch identifier
get_ssm_arch_identifier() {
  local ARCH
  ARCH=$(uname -m)

  list_contains "$ARCH_X86" "$ARCH" && printf "386" && return 0
  list_contains "$ARCH_X86_64" "$ARCH" && printf "amd64" && return 0
  list_contains "$ARCH_ARM64" "$ARCH" && printf "arm64" && return 0
  list_contains "$ARCH_ARM32" "$ARCH" && printf "arm" && return 0
  return 1
}

# Check whether element (second parameter) is part of list (first parameter)
list_contains() {
  for element in $1; do
    if [ "$2" = "$element" ]; then
      return 0
    fi
  done
  return 1
}

# Waits until SSM agent is started (timeout 20s)
await_ssm() {
  for _ in {1..20}; do
    if systemctl -q is-active amazon-ssm-agent; then
      return
    else
      sleep 1
    fi
  done

  printf >&2 "SSM startup timed out!\n"
  exit $EXIT_CODE_SSM_START_FAILED
}

is_registered() {
  [ -f "$REGISTRATION_PATH" ]
}

register_ssm() {
  amazon-ssm-agent -y -register -code "$ACTIVATION_CODE" -id "$ACTIVATION_ID" -region "$REGION"
}

main() {
  check_arch
  parse_args "$@"
  verify_args "$@"

  # Check for SSM and install it if not present
  test "$(command -v amazon-ssm-agent)" || install_ssm

  # already registered and force override arg not set
  if is_registered && [ -z ${OVERRIDE+x} ]; then
    printf >&2 "System already registered
Registration info: %s\n
You can force re-registration by adding '--override' to the command invocation.\n" "$(cat "$REGISTRATION_PATH")"
    exit $EXIT_CODE_ALREADY_REGISTERED
  fi

  systemctl -q is-active amazon-ssm-agent && systemctl stop amazon-ssm-agent
  register_ssm

  # Enable SSM to start at boot
  systemctl -q is-enabled amazon-ssm-agent || systemctl enable amazon-ssm-agent
  systemctl start amazon-ssm-agent && await_ssm
}

main "$@"
