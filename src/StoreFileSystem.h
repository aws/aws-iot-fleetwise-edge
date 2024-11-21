// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <aws/store/common/expected.hpp>
#include <aws/store/common/slices.hpp>
#include <aws/store/filesystem/filesystem.hpp>
#include <boost/filesystem.hpp>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{
namespace Store
{
class PosixFileLike : public aws::store::filesystem::FileLike
{
    std::mutex _read_lock{};
    boost::filesystem::path _path;
    FILE *_f = nullptr;

public:
    explicit PosixFileLike( boost::filesystem::path &&path );
    PosixFileLike( PosixFileLike && ) = delete;
    PosixFileLike( PosixFileLike & ) = delete;
    PosixFileLike &operator=( PosixFileLike & ) = delete;
    PosixFileLike &operator=( PosixFileLike && ) = delete;

    ~PosixFileLike() override;

    aws::store::filesystem::FileError open() noexcept;

    aws::store::common::Expected<aws::store::common::OwnedSlice, aws::store::filesystem::FileError> read(
        uint32_t begin, uint32_t end ) override;

    aws::store::filesystem::FileError append( aws::store::common::BorrowedSlice data ) override;

    aws::store::filesystem::FileError flush() override;

    void sync() override;

    aws::store::filesystem::FileError truncate( uint32_t max ) override;
};

class PosixFileSystem : public aws::store::filesystem::FileSystemInterface
{
protected:
    bool _initialized{ false };
    boost::filesystem::path _base_path;

public:
    explicit PosixFileSystem( boost::filesystem::path base_path );

    aws::store::common::Expected<std::unique_ptr<aws::store::filesystem::FileLike>, aws::store::filesystem::FileError>
    open( const std::string &identifier ) override;

    bool exists( const std::string &identifier ) override;

    aws::store::filesystem::FileError rename( const std::string &old_id, const std::string &new_id ) override;

    aws::store::filesystem::FileError remove( const std::string &id ) override;

    aws::store::common::Expected<std::vector<std::string>, aws::store::filesystem::FileError> list() override;
};

aws::store::filesystem::FileError errnoToFileError( const int err, const std::string &str = {} );

} // namespace Store
} // namespace IoTFleetWise
} // namespace Aws
