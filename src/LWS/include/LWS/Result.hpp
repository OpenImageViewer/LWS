#pragma once
namespace LWS
{
    enum class Result
    {
        Success = 0,
        Failure,
        InvalidState,
        NotSupported,
        AlreadyCreated,
        NotCreated
    };
}
