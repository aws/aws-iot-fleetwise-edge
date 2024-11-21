// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "StoreFileSystem.h"
#include <aws/store/common/expected.hpp>
#include <aws/store/common/slices.hpp>
#include <aws/store/filesystem/filesystem.hpp>
#include <boost/system/error_code.hpp>
#include <cerrno>
#include <cstdio>
#include <mutex>
#include <tuple>
#include <unistd.h>
#include <utility>

namespace Aws
{
namespace IoTFleetWise
{
namespace Store
{
static void
sync( int fileno )
{
    // Only sync data if available on this OS. Otherwise, just fsync.
#if _POSIX_SYNCHRONIZED_IO > 0
    std::ignore = fdatasync( fileno );
#else
    std::ignore = fsync( fileno );
#endif
}

aws::store::filesystem::FileError
errnoToFileError( const int err, const std::string &str )
{
    switch ( err )
    {
    case EACCES:
        return aws::store::filesystem::FileError{ aws::store::filesystem::FileErrorCode::AccessDenied,
                                                  str + " Access denied" };
    case EDQUOT:
        return aws::store::filesystem::FileError{ aws::store::filesystem::FileErrorCode::DiskFull,
                                                  str + " User inode/disk block quota exhausted" };
    case EINVAL:
        return aws::store::filesystem::FileError{ aws::store::filesystem::FileErrorCode::InvalidArguments,
                                                  str + " Unknown invalid arguments" };
    case EISDIR:
        return aws::store::filesystem::FileError{ aws::store::filesystem::FileErrorCode::InvalidArguments,
                                                  str +
                                                      " Path cannot be opened for writing because it is a directory" };
    case ELOOP:
        return aws::store::filesystem::FileError{ aws::store::filesystem::FileErrorCode::InvalidArguments,
                                                  str + " Too many symbolic links" };
    case EMFILE: // fallthrough
    case ENFILE:
        return aws::store::filesystem::FileError{ aws::store::filesystem::FileErrorCode::TooManyOpenFiles,
                                                  str + " Too many open files. Consider raising limits." };
    case ENOENT:
        return aws::store::filesystem::FileError{ aws::store::filesystem::FileErrorCode::FileDoesNotExist,
                                                  str + " Path does not exist" };
    case EFBIG:
        return aws::store::filesystem::FileError{ aws::store::filesystem::FileErrorCode::InvalidArguments,
                                                  str + " File is too large" };
    case EIO:
        return aws::store::filesystem::FileError{ aws::store::filesystem::FileErrorCode::IOError,
                                                  str + " Unknown IO error" };
    case ENOSPC:
        return aws::store::filesystem::FileError{ aws::store::filesystem::FileErrorCode::DiskFull, str + " Disk full" };
    default:
        return aws::store::filesystem::FileError{ aws::store::filesystem::FileErrorCode::Unknown,
                                                  str + " Unknown error code: " + std::to_string( err ) };
    }
}

PosixFileLike::PosixFileLike( boost::filesystem::path &&path )
    : _path( std::move( path ) )
{
}

PosixFileLike::~PosixFileLike()
{
    if ( _f != nullptr )
    {
        std::ignore = std::fclose( _f );
    }
}

aws::store::filesystem::FileError
PosixFileLike::open() noexcept
{
    _f = std::fopen( _path.c_str(), "ab+" );
    if ( _f == nullptr )
    {
        // coverity[autosar_cpp14_m19_3_1_violation] fopen gives us errors via errno
        // coverity[misra_cpp_2008_rule_19_3_1_violation] fopen gives us errors via errno
        return errnoToFileError( errno );
    }
    return aws::store::filesystem::FileError{ aws::store::filesystem::FileErrorCode::NoError, {} };
}

aws::store::common::Expected<aws::store::common::OwnedSlice, aws::store::filesystem::FileError>
PosixFileLike::read( uint32_t begin, uint32_t end )
{
    if ( end < begin )
    {
        return aws::store::filesystem::FileError{ aws::store::filesystem::FileErrorCode::InvalidArguments,
                                                  "End must be after the beginning" };
    }
    if ( end == begin )
    {
        return aws::store::common::OwnedSlice{ 0U };
    }

    std::lock_guard<std::mutex> lock{ _read_lock };
    clearerr( _f );
    auto d = aws::store::common::OwnedSlice{ ( end - begin ) };
    if ( std::fseek( _f, static_cast<off_t>( begin ), SEEK_SET ) != 0 )
    {
        // coverity[autosar_cpp14_m19_3_1_violation] fseek gives us errors via errno
        // coverity[misra_cpp_2008_rule_19_3_1_violation] fseek gives us errors via errno
        return errnoToFileError( errno );
    }
    if ( std::fread( d.data(), d.size(), 1U, _f ) != 1U )
    {
        if ( feof( _f ) != 0 )
        {
            return { aws::store::filesystem::FileError{ aws::store::filesystem::FileErrorCode::EndOfFile, {} } };
        }
        // coverity[autosar_cpp14_m19_3_1_violation] fread gives us errors via errno
        // coverity[misra_cpp_2008_rule_19_3_1_violation] fread gives us errors via errno
        return errnoToFileError( errno );
    }
    return d;
}

aws::store::filesystem::FileError
PosixFileLike::append( aws::store::common::BorrowedSlice data )
{
    clearerr( _f );
    if ( fwrite( data.data(), data.size(), 1U, _f ) != 1U )
    {
        // coverity[autosar_cpp14_m19_3_1_violation] fwrite gives us errors via errno
        // coverity[misra_cpp_2008_rule_19_3_1_violation] fwrite gives us errors via errno
        return errnoToFileError( errno );
    }
    return { aws::store::filesystem::FileErrorCode::NoError, {} };
}

aws::store::filesystem::FileError
PosixFileLike::flush()
{
    if ( fflush( _f ) == 0 )
    {
        return aws::store::filesystem::FileError{ aws::store::filesystem::FileErrorCode::NoError, {} };
    }
    // coverity[autosar_cpp14_m19_3_1_violation] fflush gives us errors via errno
    // coverity[misra_cpp_2008_rule_19_3_1_violation] fflush gives us errors via errno
    return errnoToFileError( errno );
}

void
PosixFileLike::sync()
{
    Aws::IoTFleetWise::Store::sync( fileno( _f ) );
}

aws::store::filesystem::FileError
PosixFileLike::truncate( uint32_t max )
{
    // Flush buffers before truncating since truncation is operating on the FD directly rather than the file
    // stream
    std::ignore = flush();
    if ( ftruncate( fileno( _f ), static_cast<off_t>( max ) ) != 0 )
    {
        // coverity[autosar_cpp14_m19_3_1_violation] ftruncate gives us errors via errno
        // coverity[misra_cpp_2008_rule_19_3_1_violation] ftruncate gives us errors via errno
        return errnoToFileError( errno );
    }
    return flush();
}

PosixFileSystem::PosixFileSystem( boost::filesystem::path base_path )
    : _base_path( std::move( base_path ) )
{
}

aws::store::common::Expected<std::unique_ptr<aws::store::filesystem::FileLike>, aws::store::filesystem::FileError>
PosixFileSystem::open( const std::string &identifier )
{
    if ( !_initialized )
    {
        boost::system::error_code ec;
        boost::filesystem::create_directories( _base_path, ec );
        if ( ec.failed() )
        {
            return aws::store::filesystem::FileError{ aws::store::filesystem::FileErrorCode::Unknown, ec.what() };
        }
        _initialized = true;
    }

    auto f = std::make_unique<PosixFileLike>( _base_path / boost::filesystem::path{ identifier } );
    auto res = f->open();
    if ( res.ok() )
    {
        return { std::move( f ) };
    }
    return res;
}

bool
PosixFileSystem::exists( const std::string &identifier )
{
    return boost::filesystem::exists( _base_path / boost::filesystem::path{ identifier } );
}

aws::store::filesystem::FileError
PosixFileSystem::rename( const std::string &old_id, const std::string &new_id )
{
    boost::system::error_code ec;
    boost::filesystem::rename(
        _base_path / boost::filesystem::path{ old_id }, _base_path / boost::filesystem::path{ new_id }, ec );
    if ( !ec )
    {
        return { aws::store::filesystem::FileErrorCode::NoError, {} };
    }
    return errnoToFileError( ec.value(), ec.message() );
}

aws::store::filesystem::FileError
PosixFileSystem::remove( const std::string &id )
{
    boost::system::error_code ec;
    std::ignore = boost::filesystem::remove( _base_path / boost::filesystem::path{ id }, ec );
    if ( !ec )
    {
        return { aws::store::filesystem::FileErrorCode::NoError, {} };
    }
    return errnoToFileError( ec.value(), ec.message() );
}

aws::store::common::Expected<std::vector<std::string>, aws::store::filesystem::FileError>
PosixFileSystem::list()
{
    std::vector<std::string> output;
    for ( const auto &entry : boost::filesystem::directory_iterator( _base_path ) )
    {
        output.emplace_back( entry.path().filename().string() );
    }
    return output;
}
} // namespace Store
} // namespace IoTFleetWise
} // namespace Aws
