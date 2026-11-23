#pragma once
#import <UIKit/UIKit.h>
#import <SceneKit/SceneKit.h>

// Mobile-first diagnostic camera for the *synthetic staging layout* made of
// user-imported XModel meshes. NOT the BO1 engine, controls, or physics.
@interface BO1ExploreViewController : UIViewController
@property(nonatomic, strong, readonly) SCNView* explorerView;
@property(nonatomic, strong, readonly) SCNNode* explorerCamera;
- (instancetype)initWithScene:(SCNScene*)scene span:(CGFloat)span
    eyeHeight:(CGFloat)eyeHeight;
@end
