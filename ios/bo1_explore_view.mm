#import "bo1_explore_view.h"
#import <QuartzCore/QuartzCore.h>
#include <platform/bo1_explore_motion.h>
#include <algorithm>
#include <array>
#include <cmath>

@interface BO1ExploreViewController ()
{
    std::array<bool, 4> _moving;
    float _yaw;
    float _pitch;
    NSTimeInterval _lastFrame;
}
@property(nonatomic, strong) SCNScene* renderedScene;
@property(nonatomic, strong, readwrite) SCNView* explorerView;
@property(nonatomic, strong, readwrite) SCNNode* explorerCamera;
@property(nonatomic, strong) CADisplayLink* ticker;
@property(nonatomic) CGFloat worldSpan;
@property(nonatomic) CGFloat eyeHeight;
@end

@implementation BO1ExploreViewController
- (instancetype)initWithScene:(SCNScene*)scene span:(CGFloat)span
    eyeHeight:(CGFloat)eyeHeight
{
    self = [super init];
    if (self)
    {
        self.renderedScene = scene;
        self.worldSpan = std::max(CGFloat(0.3), span);
        self.eyeHeight = std::max(CGFloat(0.2), eyeHeight);
        std::fill(_moving.begin(), _moving.end(), false);
        _yaw = _pitch = 0;
        _lastFrame = 0;
        self.modalPresentationStyle = UIModalPresentationFullScreen;
    }
    return self;
}

- (void)viewDidLoad
{
    [super viewDidLoad];
    self.view.backgroundColor = UIColor.blackColor;
    self.explorerView = [[SCNView alloc] init];
    self.explorerView.translatesAutoresizingMaskIntoConstraints = NO;
    self.explorerView.scene = self.renderedScene;
    self.explorerView.backgroundColor = UIColor.blackColor;
    self.explorerView.allowsCameraControl = NO;
    self.explorerView.accessibilityIdentifier = @"bo1FirstPersonScene";
    [self.view addSubview:self.explorerView];
    [NSLayoutConstraint activateConstraints:@[
        [self.explorerView.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor],
        [self.explorerView.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor],
        [self.explorerView.topAnchor constraintEqualToAnchor:self.view.topAnchor],
        [self.explorerView.bottomAnchor constraintEqualToAnchor:self.view.bottomAnchor]
    ]];

    self.explorerCamera = [SCNNode node];
    self.explorerCamera.name = @"BO1 first person diagnostic camera";
    self.explorerCamera.camera = [SCNCamera camera];
    self.explorerCamera.camera.fieldOfView = 70;
    self.explorerCamera.camera.zNear = 0.02;
    self.explorerCamera.camera.zFar = std::max(200.0, double(self.worldSpan) * 35.0);
    self.explorerCamera.position = SCNVector3Make(0, self.eyeHeight, self.worldSpan * 2.5f);
    [self.renderedScene.rootNode addChildNode:self.explorerCamera];
    self.explorerView.pointOfView = self.explorerCamera;

    UIPanGestureRecognizer* look = [[UIPanGestureRecognizer alloc]
        initWithTarget:self action:@selector(lookAround:)];
    [self.explorerView addGestureRecognizer:look];

    UIButton* close = [UIButton buttonWithType:UIButtonTypeSystem];
    close.translatesAutoresizingMaskIntoConstraints = NO;
    close.accessibilityIdentifier = @"bo1ExitFirstPerson";
    UIButtonConfiguration* closeConfig = [UIButtonConfiguration filledButtonConfiguration];
    closeConfig.title = @"Volver al visor";
    closeConfig.baseBackgroundColor = [UIColor colorWithWhite:0.12 alpha:0.82];
    closeConfig.baseForegroundColor = UIColor.whiteColor;
    close.configuration = closeConfig;
    [close addTarget:self action:@selector(closeExplorer:)
        forControlEvents:UIControlEventTouchUpInside];
    [self.view addSubview:close];
    [NSLayoutConstraint activateConstraints:@[
        [close.topAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.topAnchor constant:12],
        [close.leadingAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.leadingAnchor constant:14]
    ]];

    UILabel* hint = [[UILabel alloc] init];
    hint.translatesAutoresizingMaskIntoConstraints = NO;
    hint.text = @"Arrastrá para mirar · mantené las flechas para caminar";
    hint.textColor = UIColor.whiteColor;
    hint.backgroundColor = [UIColor colorWithWhite:0.08 alpha:0.66];
    hint.font = [UIFont preferredFontForTextStyle:UIFontTextStyleCaption1];
    hint.numberOfLines = 2;
    hint.textAlignment = NSTextAlignmentCenter;
    hint.layer.cornerRadius = 8;
    hint.clipsToBounds = YES;
    [self.view addSubview:hint];
    [NSLayoutConstraint activateConstraints:@[
        [hint.topAnchor constraintEqualToAnchor:close.bottomAnchor constant:9],
        [hint.leadingAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.leadingAnchor constant:14],
        [hint.trailingAnchor constraintLessThanOrEqualToAnchor:self.view.safeAreaLayoutGuide.trailingAnchor constant:-14],
        [hint.heightAnchor constraintGreaterThanOrEqualToConstant:34]
    ]];

    UIView* pad = [[UIView alloc] init];
    pad.translatesAutoresizingMaskIntoConstraints = NO;
    pad.accessibilityIdentifier = @"bo1WalkingPad";
    [self.view addSubview:pad];
    [NSLayoutConstraint activateConstraints:@[
        [pad.leadingAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.leadingAnchor constant:20],
        [pad.bottomAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.bottomAnchor constant:-24],
        [pad.widthAnchor constraintEqualToConstant:166],
        [pad.heightAnchor constraintEqualToConstant:164]
    ]];
    NSArray<NSString*>* glyphs = @[@"↑", @"↓", @"←", @"→"];
    NSArray<NSString*>* names = @[@"Adelante", @"Atrás", @"Izquierda", @"Derecha"];
    const CGRect frames[4] = {
        CGRectMake(57, 0, 52, 52),
        CGRectMake(57, 112, 52, 52),
        CGRectMake(0, 56, 52, 52),
        CGRectMake(114, 56, 52, 52)
    };
    for (NSInteger i = 0; i < 4; ++i)
    {
        UIButton* button = [UIButton buttonWithType:UIButtonTypeSystem];
        button.frame = frames[i];
        button.tag = 901 + i;
        button.accessibilityLabel = names[i];
        UIButtonConfiguration* config = [UIButtonConfiguration filledButtonConfiguration];
        config.title = glyphs[i];
        config.baseBackgroundColor = [UIColor colorWithWhite:0.12 alpha:0.75];
        config.baseForegroundColor = UIColor.whiteColor;
        button.configuration = config;
        [button addTarget:self action:@selector(movementDown:)
            forControlEvents:UIControlEventTouchDown | UIControlEventTouchDragEnter];
        [button addTarget:self action:@selector(movementUp:)
            forControlEvents:UIControlEventTouchUpInside | UIControlEventTouchUpOutside
                | UIControlEventTouchCancel | UIControlEventTouchDragExit];
        [pad addSubview:button];
    }
}

- (void)viewDidAppear:(BOOL)animated
{
    [super viewDidAppear:animated];
    _lastFrame = 0;
    self.ticker = [CADisplayLink displayLinkWithTarget:self
        selector:@selector(stepCamera:)];
    [self.ticker addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
}

- (void)viewWillDisappear:(BOOL)animated
{
    [super viewWillDisappear:animated];
    [self.ticker invalidate];
    self.ticker = nil;
    _lastFrame = 0;
    std::fill(_moving.begin(), _moving.end(), false);
}

- (void)movementDown:(UIButton*)sender
{
    const NSInteger direction = sender.tag - 901;
    if (direction >= 0 && direction < 4)
        _moving[direction] = true;
}

- (void)movementUp:(UIButton*)sender
{
    const NSInteger direction = sender.tag - 901;
    if (direction >= 0 && direction < 4)
        _moving[direction] = false;
}

- (void)lookAround:(UIPanGestureRecognizer*)gesture
{
    CGPoint delta = [gesture translationInView:self.explorerView];
    [gesture setTranslation:CGPointZero inView:self.explorerView];
    bo1::explore::CameraState state{};
    state.yaw = _yaw;
    state.pitch = _pitch;
    bo1::explore::Turn(state, float(delta.x), float(delta.y));
    _yaw = state.yaw;
    _pitch = state.pitch;
    self.explorerCamera.eulerAngles = SCNVector3Make(_pitch, _yaw, 0);
}

- (void)stepCamera:(CADisplayLink*)link
{
    const NSTimeInterval dt = _lastFrame <= 0 ? 1.0 / 60.0
        : std::max(0.0, std::min(0.05, link.timestamp - _lastFrame));
    _lastFrame = link.timestamp;
    const float forward = int(_moving[0]) - int(_moving[1]);
    const float side = int(_moving[3]) - int(_moving[2]);
    SCNVector3 position = self.explorerCamera.position;
    bo1::explore::CameraState state{};
    state.x = position.x;
    state.y = position.y;
    state.z = position.z;
    state.yaw = _yaw;
    bo1::explore::Step(state, forward, side, float(dt), float(self.worldSpan));
    self.explorerCamera.position = SCNVector3Make(state.x, state.y, state.z);
}

- (void)viewDidDisappear:(BOOL)animated
{
    [super viewDidDisappear:animated];
    if (self.isBeingDismissed)
        [self.explorerCamera removeFromParentNode];
}

- (void)closeExplorer:(id)sender
{
    [self dismissViewControllerAnimated:YES completion:nil];
}
@end
