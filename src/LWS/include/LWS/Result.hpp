#pragma once
namespace LWS
{
    enum class Result
    {
        Success = 0,
        Failure,
        InvalidArgument,
        InvalidState,
        NotSupported,
        AlreadyCreated,
        NotCreated,
        PlatformNotInitialized,
        IncompatibleThreadApartment
    };
}
