#pragma once
namespace LWSUI::internal
{
    // Restore the caller's state even when callbacks nest or throw.
    class ScopedFlag
    {
      public:

        ScopedFlag(bool& flag, bool value) : flag_(flag), previous_(flag) { flag_ = value; }
        ~ScopedFlag() { flag_ = previous_; }
        ScopedFlag(const ScopedFlag&) = delete;
        ScopedFlag& operator=(const ScopedFlag&) = delete;

      private:

        bool& flag_;
        bool previous_;
    };
}  // namespace LWSUI::internal
