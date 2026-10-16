#ifndef CLIPBOARD_H
#define CLIPBOARD_H

#include <memory>
#include <vector>

#include "base/signal.h"

class WaylandClipboard;

// Keeps the clipboard's contents once the application that copied them quits:
// every new selection is read and offered again from this process.
class Clipboard
{
public:
    Clipboard();
    ~Clipboard();

    Clipboard(const Clipboard &) = delete;
    Clipboard &operator=(const Clipboard &) = delete;

private:
    void onDataChanged();

    std::unique_ptr<WaylandClipboard> m_waylandClipboard;
    std::vector<cutefish::base::Subscription> m_subscriptions;
};

#endif // CLIPBOARD_H
