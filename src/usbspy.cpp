#include "usbs.h"

using Nan::AsyncProgressQueueWorker;
using Nan::AsyncQueueWorker;
using Nan::Callback;
using Nan::HandleScope;
using Nan::New;
using Nan::To;

std::mutex spyMutext;
std::condition_variable spyConditionVar;
bool ready = false;

#define __TEST_MODE__ 1

const typename AsyncProgressQueueWorker<Device>::ExecutionProgress *globalProgress;

void processData(const typename AsyncProgressQueueWorker<Device>::ExecutionProgress &progress)
{
	globalProgress = &progress;

	PopulateAvailableUSBDeviceList(false);

	std::thread worker(SpyingThread);

#ifdef __TEST_MODE__
	worker.join();
#else
	worker.detach();
#endif // __TEST_MODE__
}

DWORD WINAPI SpyingThread()
{
	char className[MAX_THREAD_WINDOW_NAME];
	_snprintf_s(className, MAX_THREAD_WINDOW_NAME, "ListnerThreadUsbDetection_%d", GetCurrentThreadId());

	WNDCLASSA wincl = {0};
	wincl.hInstance = GetModuleHandle(0);
	wincl.lpszClassName = className;
	wincl.lpfnWndProc = SpyCallback;

	if (!RegisterClassA(&wincl))
	{
		DWORD le = GetLastError();
		printf("RegisterClassA() failed [Error: %x]\r\n", le);
		return 1;
	}

	HWND hwnd = CreateWindowExA(WS_EX_TOPMOST, className, className, 0, 0, 0, 0, 0, NULL, 0, 0, 0);
	if (!hwnd)
	{
		DWORD le = GetLastError();
		printf("CreateWindowExA() failed [Error: %x]\r\n", le);
		return 1;
	}

	DEV_BROADCAST_DEVICEINTERFACE_A notifyFilter = {0};
	notifyFilter.dbcc_size = sizeof(notifyFilter);
	notifyFilter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
	notifyFilter.dbcc_classguid = {
		0xA5DCBF10L,
		0x6530,
		0x11D2,
		0x90,
		0x1F,
		0x00,
		0xC0,
		0x4F,
		0xB9,
		0x51,
		0xED};

	HDEVNOTIFY hDevNotify = RegisterDeviceNotificationA(hwnd, &notifyFilter, DEVICE_NOTIFY_WINDOW_HANDLE);
	if (!hDevNotify)
	{
		DWORD le = GetLastError();
		printf("RegisterDeviceNotificationA() failed [Error: %x]\r\n", le);
		return 1;
	}
	std::cout << "before gets message \n"
			  << std::endl;
	MSG msg;
	while (TRUE)
	{
		BOOL bRet = GetMessage(&msg, hwnd, 0, 0);
		if ((bRet == 0) || (bRet == -1))
		{
			break;
		}

		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	return 0;
}

LRESULT CALLBACK SpyCallback(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (msg == WM_DEVICECHANGE)
	{
		if (DBT_DEVICEARRIVAL == wParam || DBT_DEVICEREMOVECOMPLETE == wParam)
		{
			PDEV_BROADCAST_HDR pHdr = (PDEV_BROADCAST_HDR)lParam;
			PDEV_BROADCAST_DEVICEINTERFACE pDevInf;

			if (pHdr->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE)
			{
				pDevInf = (PDEV_BROADCAST_DEVICEINTERFACE)pHdr;
				Device *device;

				std::this_thread::sleep_for(std::chrono::seconds(3));

				device = PopulateAvailableUSBDeviceList(DBT_DEVICEARRIVAL != wParam);

				if (ready)
				{
#ifndef __TEST_MODE__
					globalProgress->Send(device, 1);
#endif // !__TEST_MODE__
				}
			}
		}
	}

	return 1;
}

template <typename T>
class ProgressQueueWorker : public AsyncProgressQueueWorker<T>
{
  public:
	ProgressQueueWorker(Callback *callback, Callback *progress) : AsyncProgressQueueWorker<T>(callback), progress(progress)
	{
	}

	~ProgressQueueWorker()
	{
		delete progress;
	}

	void Execute(const typename AsyncProgressQueueWorker<T>::ExecutionProgress &progress)
	{
		std::unique_lock<std::mutex> lk(spyMutext);

		processData(progress);

		while (ready)
		{
			spyConditionVar.wait(lk);
		}

		lk.unlock();
		spyConditionVar.notify_one();
	}

	void HandleProgressCallback(const T *data, size_t count)
	{
		HandleScope scope;
		v8::Local<v8::Object> obj = Nan::New<v8::Object>();

		Nan::Set(
			obj,
			Nan::New("deviceNumber").ToLocalChecked(),
			New<v8::Number>(data->deviceNumber));
		Nan::Set(
			obj,
			Nan::New("deviceStatus").ToLocalChecked(),
			New<v8::Number>(data->deviceStatus));
		Nan::Set(
			obj,
			Nan::New("vendorId").ToLocalChecked(),
			New<v8::String>(data->vendorId.c_str()).ToLocalChecked());
		Nan::Set(
			obj,
			Nan::New("serialNumber").ToLocalChecked(),
			New<v8::String>(data->serialNumber.c_str()).ToLocalChecked());
		Nan::Set(
			obj,
			Nan::New("productId").ToLocalChecked(),
			New<v8::String>(data->productId.c_str()).ToLocalChecked());
		Nan::Set(
			obj,
			Nan::New("driveLetter").ToLocalChecked(),
			New<v8::String>(data->driveLetter.c_str()).ToLocalChecked());

		v8::Local<v8::Value> argv[] = {obj};

#ifndef __TEST_MODE__
		progress->Call(1, argv);
#endif // __TEST_MODE__
	}

  private:
	Callback *progress;
};

NAN_METHOD(SpyOn)
{
#ifdef __TEST_MODE__
	Callback *progress = new Callback();
	Callback *callback = new Callback();
#else
	Callback *progress = new Callback(To<v8::Function>(info[0]).ToLocalChecked());
	Callback *callback = new Callback(To<v8::Function>(info[1]).ToLocalChecked());
#endif // __TEST_MODE__

	AsyncQueueWorker(new ProgressQueueWorker<Device>(callback, progress));
}

void StartSpying()
{
	{
		std::lock_guard<std::mutex> lk(spyMutext);
		ready = true;
		std::cout << "main() signals data ready for processing\n"
				  << std::endl;
	}
	spyConditionVar.notify_one();

#ifdef __TEST_MODE__
	New<v8::FunctionTemplate>(SpyOn)->GetFunction()->CallAsConstructor(0, {});
#endif // !__TEST_MODE__
}

NAN_METHOD(SpyOff)
{
	{
		std::lock_guard<std::mutex> lk(spyMutext);
		ready = false;
		std::cout << "main() signals data  not ready for processing\n"
				  << std::endl;
	}
	spyConditionVar.notify_one();
}

NAN_MODULE_INIT(Init)
{
	Nan::Set(target, New<v8::String>("spyOn").ToLocalChecked(), New<v8::FunctionTemplate>(SpyOn)->GetFunction());
	Nan::Set(target, New<v8::String>("spyOff").ToLocalChecked(), New<v8::FunctionTemplate>(SpyOff)->GetFunction());
	StartSpying();
}

NODE_MODULE(usbspy, Init)