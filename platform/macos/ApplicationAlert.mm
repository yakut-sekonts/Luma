#include "platform/macos/ApplicationAlert.h"
#import <AppKit/AppKit.h>

namespace luma::macos {
void showApplicationError(const std::string &title, const std::string &message) {
    @autoreleasepool {
        NSAlert *alert = [[NSAlert alloc] init];
        alert.alertStyle = NSAlertStyleCritical;
        NSString *titleText = [NSString stringWithUTF8String:title.c_str()];
        NSString *messageText = [NSString stringWithUTF8String:message.c_str()];
        alert.messageText = titleText ? titleText : NSLocalizedString(@"Luma", @"Application name");
        alert.informativeText = messageText ? messageText : @"";
        [NSApp activate];
        [alert runModal];
    }
}
} // namespace luma::macos
