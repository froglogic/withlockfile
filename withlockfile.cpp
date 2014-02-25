#include <iostream>
#include <stdexcept>

#include <windows.h>
#include <shlwapi.h>

class Win32Error : public std::runtime_error
{
public:
    Win32Error( const char *what, DWORD errorCode_ )
        : std::runtime_error( what )
        , errorCode( errorCode_ )
    {
    }

    const DWORD errorCode;
};

static std::string enforceExeExtension( const std::string &s )
{
    std::string exe = s;
    const std::string::size_type len = exe.size();
    if ( len < 4 ||
         _stricmp( exe.substr( len - 4 ).c_str(), ".exe" ) != 0 ) {
        exe += ".exe";
    }
    return exe;
}

static std::string quoteArgument( std::string arg )
{
    const std::string::size_type space = arg.find_first_of( " \t" );
    if ( space != std::string::npos ) {
        std::string s = "\"";
        s += arg;
        s += '"';
        return s;
    }
    return arg;
}

int main( int argc, char **argv )
{
    try {
        if ( argc < 3 ) {
            std::cerr << "usage: withlockfile <lockfile> <command> [args..]\n";
            return 1;
        }

        HANDLE f = ::CreateFileA( argv[1],
                                  GENERIC_READ, /* required by LockFileEx */
                                  FILE_SHARE_READ, /* allow concurrent opening */
                                  NULL,
                                  OPEN_ALWAYS,
                                  FILE_ATTRIBUTE_READONLY,
                                  NULL );
        if ( f == INVALID_HANDLE_VALUE ) {
            throw Win32Error( "CreateFileA", ::GetLastError() );
        }

        OVERLAPPED ol;
        ::ZeroMemory( &ol, sizeof( ol ) );

        /* For some unknown reason, LockFileEx fails with ERROR_NETNAME_DELETED
         * every now and then. We couldn't determine the reason, let's just
         * ignore this error for a while if it occurs - maybe it's some
         * network instability?
         */
        {
            bool lockingSucceeded = false;
            for ( int i = 0; i < 3; ++i ) {
                if ( ::LockFileEx( f, LOCKFILE_EXCLUSIVE_LOCK, 0, 1, 0, &ol ) ) {
                    lockingSucceeded = true;
                    break;
                }

                const DWORD errorCode = ::GetLastError();
                if ( errorCode != ERROR_NETNAME_DELETED ) {
                    throw Win32Error( "LockFileEx", errorCode );
                }
            }

            if ( !lockingSucceeded ) {
                throw Win32Error( "LockFileEx", ERROR_NETNAME_DELETED );
            }
        }

        std::string executable = enforceExeExtension( argv[2] );
        {
            // According to a comment on the PathSearchAndQualify function
            // at http://msdn.microsoft.com/en-us/library/bb773751(VS.85).aspx
            // the buffer must be at least MAX_PATH in size.
            char buf[MAX_PATH] = { 0 };
            if ( !::PathSearchAndQualifyA( executable.c_str(),
                                           buf, sizeof( buf ) ) ) {
                throw Win32Error( "PathSearchAndQualifyA", ::GetLastError() );
            }
            executable = buf;
        }

        std::string commandLine = quoteArgument( executable );
        {
            for ( int i = 3; i < argc; ++i ) {
                commandLine += ' ';
                commandLine += quoteArgument( argv[i] );
            }
        }

        PROCESS_INFORMATION pi = { 0 };

        STARTUPINFO si = { sizeof( si ) };
        si.hStdError = ::GetStdHandle( STD_ERROR_HANDLE );
        si.hStdOutput = ::GetStdHandle( STD_OUTPUT_HANDLE );
        si.hStdInput = ::GetStdHandle( STD_INPUT_HANDLE );

        if ( ::CreateProcessA( executable.c_str(),
                               const_cast<char *>( commandLine.c_str() ),
                               NULL,
                               NULL,
                               TRUE,
                               CREATE_SUSPENDED,
                               NULL,
                               NULL,
                               &si,
                               &pi ) == FALSE ) {
            throw Win32Error( "CreateProcessA", ::GetLastError() );
        }

        HANDLE jobObject = ::CreateJobObject( NULL, NULL );
        if ( !jobObject ) {
            throw Win32Error( "CreateJobObject", ::GetLastError() );
        }

        {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION jobObjectInfo = { 0 };
            jobObjectInfo.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            if ( !::SetInformationJobObject( jobObject,
                                             JobObjectExtendedLimitInformation,
                                             &jobObjectInfo,
                                             sizeof( jobObjectInfo ) ) ) {
                ::CloseHandle( jobObject );
                throw Win32Error( "SetInformationJobObject", ::GetLastError() );
            }
        }

        /* Don't bother reporting access denied with AssignProcessToJobObject
         * because it's quite common for this to happen on Windows 7 and
         * earlier if withlockfile is already part of a job object.
         */
        if ( !::AssignProcessToJobObject( jobObject, pi.hProcess ) &&
             ::GetLastError() != ERROR_ACCESS_DENIED ) {
            throw Win32Error( "AssignProcessToJobObject", ::GetLastError() );
        }

        if ( ::ResumeThread( pi.hThread ) == -1 ) {
            throw Win32Error( "ResumeThread", ::GetLastError() );
        }

        if ( ::WaitForSingleObject( pi.hProcess, INFINITE ) == WAIT_FAILED ) {
            throw Win32Error( "WaitForSingleObject", ::GetLastError() );
        }

        DWORD exitCode;
        if ( ::GetExitCodeProcess( pi.hProcess, &exitCode ) == FALSE ) {
            throw Win32Error( "GetExitCodeProcess", ::GetLastError() );
        }

        if ( ::UnlockFileEx( f, 0, 1, 0, &ol ) == FALSE ) {
            throw Win32Error( "UnlockFileEx", ::GetLastError() );
        }

        if ( ::CloseHandle( f ) == FALSE ) {
            throw Win32Error( "CloseHandle", ::GetLastError() );
        }

        return 0;
    } catch ( const Win32Error &e ) {
        /* The MSDN documentation for FormatMessage says that the buf cannot
         * be larger than 64K bytes.
         */
        char buf[65536 / sizeof(wchar_t)] = { L'0' };

        const DWORD result = ::FormatMessageA(
            FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
            NULL,
            e.errorCode,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            buf,
            sizeof( buf ),
            NULL );

        /* It seems that many error messages end in a newline, but I don't want
         * them, so strip them.
         */
        const size_t len = strlen( buf );
        if ( len > 1 && buf[len - 2] == '\r' && buf[len - 1] == '\n' ) {
            buf[len - 2] = '\0';
        }

        std::cerr << "error: " << e.what() << " failed: "
            << buf << " (code " << e.errorCode << ")"
            << std::endl;

        return e.errorCode;
    } catch ( const std::exception &e ) {
        std::cerr << "error: " << e.what() << std::endl;
        return 1;
    }
}

