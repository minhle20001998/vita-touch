#pragma once
#include <string>

namespace ime {
    bool init();
    // Opens the system on-screen keyboard. Returns true if it opened.
    // Call update() each frame while open; when finished, read text().
    bool open(const std::string& title, const std::string& initialText, int maxLen);
    bool isOpen();
    void update();                    // advances dialog; call once per frame
    bool finished();                  // true the frame the dialog closed
    bool cancelled();                 // user backed out
    const std::string& text();        // entered text (UTF-8)
}
