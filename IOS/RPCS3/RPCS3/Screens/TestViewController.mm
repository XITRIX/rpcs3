//
//  TestViewController.m
//  RPCS3
//
//  Created by Даниил Виноградов on 12.01.2025.
//

#import "TestViewController.h"

#include <EMU/System.h>

@interface TestViewController ()
@property (strong, nonatomic) IBOutlet UIButton *installFirmwareButton;
@end

@implementation TestViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    // Do any additional setup after loading the view.

    
}

- (IBAction)installFirmwareAction:(UIButton *)sender {
    Emu.CallFromMainThread([]()
    {
        
    }, nullptr, false);

}

@end
