// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "StoreFileSystem.h"
#include <aws/store/common/expected.hpp>
#include <aws/store/common/slices.hpp>
#include <aws/store/common/util.hpp>
#include <aws/store/filesystem/filesystem.hpp>
#include <cerrno>
#include <gtest/gtest.h>

namespace Aws
{
namespace IoTFleetWise
{
namespace Store
{

class FileSystemTest : public ::testing::Test
{
protected:
    void
    SetUp() override
    {
    }

    void
    TearDown() override
    {
    }
};

TEST_F( FileSystemTest, errnoToFileErrorTest )
{
    auto res = errnoToFileError( EACCES, "test" );
    EXPECT_EQ( res.code, aws::store::filesystem::FileErrorCode::AccessDenied );
    EXPECT_EQ( res.msg, "test Access denied" );
    EXPECT_FALSE( res.ok() );

    res = errnoToFileError( EDQUOT, "test" );
    EXPECT_EQ( res.code, aws::store::filesystem::FileErrorCode::DiskFull );
    EXPECT_EQ( res.msg, "test User inode/disk block quota exhausted" );
    EXPECT_FALSE( res.ok() );

    res = errnoToFileError( EINVAL, "test" );
    EXPECT_EQ( res.code, aws::store::filesystem::FileErrorCode::InvalidArguments );
    EXPECT_EQ( res.msg, "test Unknown invalid arguments" );
    EXPECT_FALSE( res.ok() );

    res = errnoToFileError( EISDIR, "test" );
    EXPECT_EQ( res.code, aws::store::filesystem::FileErrorCode::InvalidArguments );
    EXPECT_EQ( res.msg, "test Path cannot be opened for writing because it is a directory" );
    EXPECT_FALSE( res.ok() );

    res = errnoToFileError( ELOOP, "test" );
    EXPECT_EQ( res.code, aws::store::filesystem::FileErrorCode::InvalidArguments );
    EXPECT_EQ( res.msg, "test Too many symbolic links" );
    EXPECT_FALSE( res.ok() );

    res = errnoToFileError( EMFILE, "test" );
    EXPECT_EQ( res.code, aws::store::filesystem::FileErrorCode::TooManyOpenFiles );
    EXPECT_EQ( res.msg, "test Too many open files. Consider raising limits." );
    EXPECT_FALSE( res.ok() );

    res = errnoToFileError( ENFILE, "test" );
    EXPECT_EQ( res.code, aws::store::filesystem::FileErrorCode::TooManyOpenFiles );
    EXPECT_EQ( res.msg, "test Too many open files. Consider raising limits." );
    EXPECT_FALSE( res.ok() );

    res = errnoToFileError( ENOENT, "test" );
    EXPECT_EQ( res.code, aws::store::filesystem::FileErrorCode::FileDoesNotExist );
    EXPECT_EQ( res.msg, "test Path does not exist" );
    EXPECT_FALSE( res.ok() );

    res = errnoToFileError( EFBIG, "test" );
    EXPECT_EQ( res.code, aws::store::filesystem::FileErrorCode::InvalidArguments );
    EXPECT_EQ( res.msg, "test File is too large" );
    EXPECT_FALSE( res.ok() );

    res = errnoToFileError( EIO, "test" );
    EXPECT_EQ( res.code, aws::store::filesystem::FileErrorCode::IOError );
    EXPECT_EQ( res.msg, "test Unknown IO error" );
    EXPECT_FALSE( res.ok() );

    res = errnoToFileError( ENOSPC, "test" );
    EXPECT_EQ( res.code, aws::store::filesystem::FileErrorCode::DiskFull );
    EXPECT_EQ( res.msg, "test Disk full" );
    EXPECT_FALSE( res.ok() );

    res = errnoToFileError( -1, "test" );
    EXPECT_EQ( res.code, aws::store::filesystem::FileErrorCode::Unknown );
    EXPECT_EQ( res.msg, "test Unknown error code: -1" );
    EXPECT_FALSE( res.ok() );
}

TEST_F( FileSystemTest, PosixFileLikeOpenMissing )
{
    PosixFileLike missing( "dummy/missing.txt" );
    auto res = missing.open();
    EXPECT_EQ( res.code, aws::store::filesystem::FileErrorCode::FileDoesNotExist );
    EXPECT_EQ( res.msg, " Path does not exist" );
    EXPECT_FALSE( res.ok() );
}

TEST_F( FileSystemTest, PosixFileLikeReadEndBeforeBegin )
{
    PosixFileLike dummy( "dummy" );
    auto res = dummy.read( 10, 0 );
    EXPECT_EQ( res.err().code, aws::store::filesystem::FileErrorCode::InvalidArguments );
    EXPECT_EQ( res.err().msg, "End must be after the beginning" );
    EXPECT_FALSE( res.ok() );
}

TEST_F( FileSystemTest, PosixFileLikeReadEndEqualsBegin )
{
    PosixFileLike dummy( "dummy" );
    auto res = dummy.read( 10, 10 );
    EXPECT_EQ( res.val().size(), 0 );
    EXPECT_TRUE( res.ok() );
}

} // namespace Store
} // namespace IoTFleetWise
} // namespace Aws
