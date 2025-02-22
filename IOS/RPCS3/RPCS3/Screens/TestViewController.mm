//
//  TestViewController.m
//  RPCS3
//
//  Created by Даниил Виноградов on 12.01.2025.
//

#import "TestViewController.h"

#include "RPCS3.hpp"

@interface TestViewController ()
@property (strong, nonatomic) IBOutlet UIButton *installFirmwareButton;
@end

@implementation TestViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    // Do any additional setup after loading the view.

    
}

- (IBAction)installFirmwareAction:(UIButton *)sender {
    RPCS3::getShared()->callFromMainThread([]() {
        printf("Test\n");
    });

}

@end
