//
//  RPCS3.hpp
//  RPCS3
//
//  Created by Даниил Виноградов on 21.02.2025.
//

#pragma once

#include <functional>

struct EmuCallbacks;
class RPCS3 {
public:
	static RPCS3* getShared() { return _shared; }

	void callFromMainThread(std::function<void()> func);

private:
	RPCS3();
	static RPCS3* _shared;

	void OnEmuSettingsChange();
	EmuCallbacks CreateCallbacks();
	void InitializeCallbacks();
};
