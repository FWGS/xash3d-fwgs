//
//  PickerDelegate.h
//  Launchertest
//
//  Created by Yossef Baobaid on 14/07/2026.
//

#import <UIKit/UIkit.h>

@interface PickerDelegate : NSObject <UIDocumentPickerDelegate>

@property NSURL * __strong *url;


- (instancetype)initWithURL:(NSURL * __strong *)urlref;

@end
