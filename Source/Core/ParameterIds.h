// Parameter IDs. FROZEN from 1.0: Cubase stores automation against these
// strings (and the version hint), so renaming one silently breaks every saved
// project that automated it. Add new IDs; never edit or reuse an existing one.

#pragma once

namespace ah::id
{
    inline constexpr int stateVersion = 1;

    //  The four performance controls.
    inline constexpr const char* hit         = "hit";
    inline constexpr const char* space       = "space";
    inline constexpr const char* tail        = "tail";
    inline constexpr const char* gate        = "gate";

    //  The ADVANCED drawer.
    inline constexpr const char* after       = "after";
    inline constexpr const char* sensitivity = "sensitivity";
    inline constexpr const char* tone        = "tone";
    inline constexpr const char* width       = "width";
    inline constexpr const char* sync        = "sync";
    inline constexpr const char* output      = "output";

    inline constexpr const char* bypass      = "bypass";
}
