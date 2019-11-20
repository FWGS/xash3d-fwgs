
#include <wrl.h>
#include <wrl/async.h>
#include <Windows.UI.ViewManagement.h>
#include <Windows.applicationmodel.Core.h>
#include <Windows.ApplicationModel.UserDataAccounts.h>
#include <Windows.ApplicationModel.UserDataAccounts.Provider.h>
#include <Windows.ApplicationModel.UserDataAccounts.SystemAccess.h>
#include <Windows.Graphics.Display.h>
#include <Windows.System.h>
#include <windows.Storage.h>
#include <windows.Storage.Pickers.h>
#include <windows.Storage.AccessCache.h>

#include <SDL_events.h>

#include "winrt_interop.h"
extern "C" {
#include "common.h"
#include "keydefs.h"
#include "vid_common.h"

	void Key_Event(int key, int down);
}

using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
using namespace ABI::Windows::Foundation;
using namespace ABI::Windows::Foundation::Collections;
using namespace ABI::Windows::UI;
using namespace ABI::Windows::UI::Core;
using namespace ABI::Windows::UI::ViewManagement;
using namespace ABI::Windows::ApplicationModel;
using namespace ABI::Windows::ApplicationModel::Core;
using namespace ABI::Windows::ApplicationModel::UserDataAccounts;
using namespace ABI::Windows::Graphics::Display;
using namespace ABI::Windows::System;
using namespace ABI::Windows::Storage;
using namespace ABI::Windows::Storage::Pickers;
using namespace ABI::Windows::Storage::AccessCache;

#if 0
class ColorReference : public RuntimeClass<RuntimeClassFlags<WinRt>, __FIReference_1_Windows__CUI__CColor>
{
public:
	HRESULT get_Value(Color* value) override { return *value = color, S_OK; }
	Color color;
private:
	~ColorReference() {}
};
#endif

void WinRT_FullscreenMode_Install(int fullscreen)
{
	HRESULT hr;
	ComPtr<ICoreWindowStatic> coreWindowStatic;
	hr = Windows::Foundation::GetActivationFactory(
		HStringReference(RuntimeClass_Windows_UI_Core_CoreWindow).Get(),
		&coreWindowStatic);

	ComPtr<ICoreWindow> coreWindow;
	hr = coreWindowStatic->GetForCurrentThread(&coreWindow);

	ComPtr<IApplicationViewStatics2> applicationViewStatics2;
	hr = Windows::Foundation::GetActivationFactory(
		HStringReference(RuntimeClass_Windows_UI_ViewManagement_ApplicationView).Get(),
		&applicationViewStatics2);

	ComPtr<IApplicationViewStatics3> applicationViewStatics3;
	hr = Windows::Foundation::GetActivationFactory(
		HStringReference(RuntimeClass_Windows_UI_ViewManagement_ApplicationView).Get(),
		&applicationViewStatics3);

	ComPtr <IApplicationView> appView;
	if (hr = applicationViewStatics2->GetForCurrentView(&appView), SUCCEEDED(hr))
	{
		ComPtr <IApplicationView3> appView3;
		if (hr = appView.As(&appView3), SUCCEEDED(hr))
		{
			if(fullscreen)
			{
				boolean success;
				hr = appView3->TryEnterFullScreenMode(&success);
			}
			
			// disable the system gestures...
			hr = appView3->put_FullScreenSystemOverlayMode(FullScreenSystemOverlayMode_Minimal);

			// change title bar color
#if 0
			ComPtr<IApplicationViewTitleBar> titleBar;
			if(hr = appView3->get_TitleBar(&titleBar), SUCCEEDED(hr))
			{
				ComPtr<IColorHelperStatics> colorHelperStatics;
				hr = Windows::Foundation::GetActivationFactory(
					HStringReference(RuntimeClass_Windows_UI_Core_CoreWindow).Get(),
					&colorHelperStatics);

				static ComPtr<ColorReference> red; hr = MakeAndInitialize<ColorReference>(&red); red->color = { 255, 232, 16, 35 };
				static ComPtr<ColorReference> red2; hr = MakeAndInitialize<ColorReference>(&red2); red2->color = { 255, 181, 113, 113 };
				static ComPtr<ColorReference> red3; hr = MakeAndInitialize<ColorReference>(&red3); red3->color = { 255, 188, 47, 46 };
				static ComPtr<ColorReference> red4; hr = MakeAndInitialize<ColorReference>(&red4); red4->color = { 255, 241, 112, 122 };
				static ComPtr<ColorReference> white; hr = MakeAndInitialize<ColorReference>(&white); white->color = { 255, 255, 255, 255 };
				static ComPtr<ColorReference> black; hr = MakeAndInitialize<ColorReference>(&black); black->color = { 255, 0, 0, 0 };
				
				hr = titleBar->put_BackgroundColor(red3.Get());
				hr = titleBar->put_ButtonBackgroundColor(red3.Get());
				hr = titleBar->put_ButtonInactiveBackgroundColor(red2.Get());
				hr = titleBar->put_InactiveBackgroundColor(red2.Get());
				hr = titleBar->put_ButtonHoverBackgroundColor(red.Get());
				hr = titleBar->put_ButtonPressedBackgroundColor(red4.Get());
				
				hr = titleBar->put_ForegroundColor(white.Get());
				hr = titleBar->put_ButtonForegroundColor(white.Get());
				hr = titleBar->put_ButtonHoverForegroundColor(white.Get());
				hr = titleBar->put_ButtonPressedForegroundColor(black.Get());
				hr = titleBar->put_ButtonInactiveForegroundColor(white.Get());
				hr = titleBar->put_InactiveForegroundColor(white.Get());
			}
#endif
			// extend window
			ComPtr<ICoreApplication> coreApplication;
			hr = Windows::Foundation::GetActivationFactory(
				HStringReference(RuntimeClass_Windows_ApplicationModel_Core_CoreApplication).Get(),
				&coreApplication);

			ComPtr<ICoreApplicationView> coreApplicationView;
			hr = coreApplication->GetCurrentView(&coreApplicationView);

			ComPtr<ICoreApplicationView3> coreApplicationView3;
			hr = coreApplicationView.As(&coreApplicationView3);

			ComPtr<ICoreApplicationViewTitleBar> coreApplicationViewTitleBar;
			hr = coreApplicationView3->get_TitleBar(&coreApplicationViewTitleBar);

			hr = coreApplicationViewTitleBar->put_ExtendViewIntoTitleBar(false);
		}
	}
}

void WinRT_SaveVideoMode(int w, int h)
{
	HRESULT hr;
	ComPtr<IApplicationViewStatics3> applicationViewStatics3;
	hr = Windows::Foundation::GetActivationFactory(
		HStringReference(RuntimeClass_Windows_UI_ViewManagement_ApplicationView).Get(),
		&applicationViewStatics3);
	if(SUCCEEDED(hr))
	{
		hr = applicationViewStatics3->put_PreferredLaunchViewSize(Size{ (float)w,(float)h });
		hr = applicationViewStatics3->put_PreferredLaunchWindowingMode(vid_fullscreen->value != 0.0f ? ApplicationViewWindowingMode_FullScreen : ApplicationViewWindowingMode_PreferredLaunchViewSize);
	}
}

HRESULT WinRT_OnBackRequested(IInspectable *sender, IBackRequestedEventArgs *e)
{
	Key_Event(K_ESCAPE, 1);
	return S_OK;
}

void WinRT_BackButton_Install()
{
	HRESULT hr;
	ComPtr<ISystemNavigationManagerStatics> systemNavigationManagerStatics;
	hr = Windows::Foundation::GetActivationFactory(
		HStringReference(RuntimeClass_Windows_UI_Core_SystemNavigationManager).Get(),
		&systemNavigationManagerStatics);

	ComPtr<ISystemNavigationManager> systemNavigationManager;
	hr = systemNavigationManagerStatics->GetForCurrentView(&systemNavigationManager);

	
	ComPtr<ISystemNavigationManager2> systemNavigationManager2;
	if(hr = systemNavigationManager.As(&systemNavigationManager2), SUCCEEDED(hr))
	{
		systemNavigationManager2->put_AppViewBackButtonVisibility(AppViewBackButtonVisibility_Visible);
	}
	
	auto callback = Callback<__FIEventHandler_1_Windows__CUI__CCore__CBackRequestedEventArgs>(WinRT_OnBackRequested);
	EventRegistrationToken token;
	hr = systemNavigationManager->add_BackRequested(callback.Get(), &token);
}

float WinRT_GetDisplayDPI()
{
	HRESULT hr;
	ComPtr<IDisplayInformationStatics> displayInformationStatics;
	hr = Windows::Foundation::GetActivationFactory(
		HStringReference(RuntimeClass_Windows_Graphics_Display_DisplayInformation).Get(),
		&displayInformationStatics);

	ComPtr<IDisplayInformation> displayInformation;
	hr = displayInformationStatics->GetForCurrentView(&displayInformation);

	ResolutionScale resolutionScale;
	hr = displayInformation->get_ResolutionScale(&resolutionScale);

	double rawPixelsPerViewPixel = 0.0;
	ComPtr<IDisplayInformation2> displayInformation2;
	hr = displayInformation.As<IDisplayInformation2>(&displayInformation2);
	hr = displayInformation2->get_RawPixelsPerViewPixel(&rawPixelsPerViewPixel);

	return static_cast<float>(rawPixelsPerViewPixel);
}

static SDL_bool relative_mode;
static int show_cursor_prev;
static bool mouse_captured;
static SDL_Window* current_window;

void WinRT_SDL_PushStatus()
{
	current_window = SDL_GetKeyboardFocus();
	mouse_captured = current_window && ((SDL_GetWindowFlags(current_window) & SDL_WINDOW_MOUSE_CAPTURE) != 0);
	relative_mode = SDL_GetRelativeMouseMode();
	SDL_CaptureMouse(SDL_FALSE);
	SDL_SetRelativeMouseMode(SDL_FALSE);
	show_cursor_prev = SDL_ShowCursor(1);
	//SDL_ResetKeyboard();
}

void WinRT_SDL_PopStatus()
{
	if (current_window) {
		SDL_RaiseWindow(current_window);
		if (mouse_captured) {
			SDL_CaptureMouse(SDL_TRUE);
		}
	}

	SDL_ShowCursor(show_cursor_prev);
	SDL_SetRelativeMouseMode(relative_mode);
}

#if 0
template<class T>
void WinRT_SDL_Await(ComPtr<IAsyncOperation<T>> aso)
{
	HRESULT hr;
	Event threadCompleted(CreateEventEx(nullptr, nullptr, CREATE_EVENT_MANUAL_RESET, WRITE_OWNER | EVENT_ALL_ACCESS));
	hr = threadCompleted.IsValid() ? S_OK : HRESULT_FROM_WIN32(GetLastError());

	auto cb = Callback<IAsyncOperationCompletedHandler<T>>([&threadCompleted](IAsyncOperation<T>* asyncInfo, AsyncStatus status) mutable
		{
			SetEvent(threadCompleted.Get());

			return S_OK;
		});
	aso->put_Completed(cb.Get());
	while(WaitForSingleObjectEx(threadCompleted.Get(), IGNORE, FALSE) == WAIT_TIMEOUT)
	{
		SDL_PumpEvents();
	SwitchToThread();
	}
}
#else
template<class T>
void WinRT_SDL_Await(ComPtr<IAsyncOperation<T>> aso)
{
	HRESULT hr;
	ComPtr<IAsyncInfo> pAsyncInfo;
	hr = aso.As<IAsyncInfo>(&pAsyncInfo);

	AsyncStatus asyncStatus;

	while (1)
	{
		hr = pAsyncInfo->get_Status(&asyncStatus);

		if (SUCCEEDED(hr) && (asyncStatus != AsyncStatus::Started))
			break;

		SDL_PumpEvents();
		SwitchToThread();
	}
}
#endif
// !!! requires Capability contacts, appointments
char *WinRT_GetUserName()
{
	HRESULT hr;
	static char buffer[1024] = "Player";

	ComPtr<IUserDataAccountManagerStatics> userDataAccountManagerStatics;
	hr = GetActivationFactory(HStringReference(RuntimeClass_Windows_ApplicationModel_UserDataAccounts_UserDataAccountManager).Get(), &userDataAccountManagerStatics);

	ComPtr<IAsyncOperation<UserDataAccountStore*>> futureUserDataAccountStore;
	hr = userDataAccountManagerStatics->RequestStoreAsync(UserDataAccounts::UserDataAccountStoreAccessType_AllAccountsReadOnly, &futureUserDataAccountStore);

	WinRT_SDL_Await(futureUserDataAccountStore);

	ComPtr<IUserDataAccountStore> userDataAccountStore;
	hr = futureUserDataAccountStore->GetResults(&userDataAccountStore);

	// Access denied
	if (!userDataAccountStore)
		return buffer;

	ComPtr < IAsyncOperation<IVectorView<UserDataAccount*>*> > futurevecUserDataAccounts;
	hr = userDataAccountStore->FindAccountsAsync(&futurevecUserDataAccounts);

	WinRT_SDL_Await(futurevecUserDataAccounts);
	
	ComPtr<IVectorView<UserDataAccount*>> vecUserDataAccounts;
	hr = futurevecUserDataAccounts->GetResults(&vecUserDataAccounts);

	{
		unsigned int size = 0;
		hr = vecUserDataAccounts->get_Size(&size);
		for (unsigned int i = 0; i < size; ++i)
		{
			ComPtr<IUserDataAccount> item = nullptr;
			hr = vecUserDataAccounts->GetAt(i, &item);

			ComPtr<IUserDataAccount3> item3 = nullptr;
			hr = item.As<IUserDataAccount3>(&item3);
			
			HSTRING husername = nullptr;
			hr = item->get_UserDisplayName(&husername);
			HString username;
			username.Attach(husername);

			unsigned len = 0;
			const wchar_t* wstr = username.GetRawBuffer(&len);

			WideCharToMultiByte(CP_ACP, 0, wstr, -1, buffer, 1024, NULL, FALSE);
		}
	}

	return buffer;
}

void WinRT_ShellExecute(const char* path)
{
	wchar_t buffer[1024]{};
	MultiByteToWideChar(CP_ACP, 0, path, -1, buffer, 1024);

	HRESULT hr;
	ComPtr<ILauncherOptions> launcherOptions;
	hr = ActivateInstance(
		HStringReference(RuntimeClass_Windows_System_LauncherOptions).Get(),
		&launcherOptions);
	//hr = launcherOptions->put_DisplayApplicationPicker(true);

	ComPtr<ILauncherStatics> launcherStatics;
	hr = GetActivationFactory(
		HStringReference(RuntimeClass_Windows_System_Launcher).Get(),
		&launcherStatics);

	ComPtr<IUriRuntimeClassFactory> uriRuntimeClassFactory;
	hr = GetActivationFactory(
		HStringReference(RuntimeClass_Windows_Foundation_Uri).Get(),
		&uriRuntimeClassFactory);
	
	ComPtr<IUriRuntimeClass> uri;
	hr = uriRuntimeClassFactory->CreateUri(HStringReference(buffer).Get(), &uri);

	ComPtr <IAsyncOperation<bool>> futureLaunchUriResult;
	hr = launcherStatics->LaunchUriWithOptionsAsync(uri.Get(), launcherOptions.Get(), &futureLaunchUriResult);

	WinRT_SDL_Await(futureLaunchUriResult);
}

void WinRT_OpenGameFolderWithExplorer()
{
	HRESULT hr;
	ComPtr<IApplicationDataStatics> applicationDataStatics;
	hr = GetActivationFactory(
		HStringReference(RuntimeClass_Windows_Storage_ApplicationData).Get(),
		&applicationDataStatics);

	ComPtr<IApplicationData> applicationData;
	hr = applicationDataStatics->get_Current(&applicationData);

	ComPtr<IStorageFolder> localFolder;
	hr = applicationData->get_LocalFolder(&localFolder);

	ComPtr<ILauncherStatics> launcherStatics;
	hr = GetActivationFactory(
		HStringReference(RuntimeClass_Windows_System_Launcher).Get(),
		&launcherStatics);

	ComPtr<ILauncherStatics3> launcherStatics3;
	hr = launcherStatics.As<ILauncherStatics3>(&launcherStatics3);

	ComPtr <IAsyncOperation<bool>> futureLaunchResult;
	hr = launcherStatics3->LaunchFolderAsync(localFolder.Get(), &futureLaunchResult);

	WinRT_SDL_Await(futureLaunchResult);
}

const char* WinRT_GetGameFolderAppData()
{
	return SDL_WinRTGetFSPathUTF8(SDL_WINRT_PATH_LOCAL_FOLDER);
}
#if 0
// always denied... maybe requires "documentsLibrary"
const char* WinRT_GetGameFolderDocument()
{
	static char s_szWinRTGameDIR[MAX_PATH];
	if (s_szWinRTGameDIR[0])
		return s_szWinRTGameDIR;
	
	HRESULT hr;
	ComPtr<IKnownFoldersStatics> knownFoldersStatics;
	hr = GetActivationFactory(
		HStringReference(RuntimeClass_Windows_Storage_KnownFolders).Get(),
		&knownFoldersStatics);

	ComPtr<IStorageFolder> storageFolder;
	hr = knownFoldersStatics->get_DocumentsLibrary(&storageFolder);

	// Access deined
	if (!storageFolder)
		return WinRT_GetGameFolderAppData();

	ComPtr<IStorageItem> storageFolderItem;
	hr = storageFolder.As<IStorageItem>(&storageFolderItem);

	HSTRING hSTRPath;
	hr = storageFolderItem->get_Path(&hSTRPath);
	HString hStrPath;
	hStrPath.Attach(hSTRPath);

	unsigned len = 0;
	const wchar_t* wstr = hStrPath.GetRawBuffer(&len);

	WideCharToMultiByte(CP_ACP, 0, wstr, -1, s_szWinRTGameDIR, 1024, NULL, FALSE);
	
	return s_szWinRTGameDIR;
}

const char *WinRT_GetGameFolderCustom()
{
	static char s_szWinRTGameDIR[MAX_PATH];
	if (s_szWinRTGameDIR[0])
		return s_szWinRTGameDIR;

	HRESULT hr;

	ComPtr<IStorageApplicationPermissionsStatics> storageApplicationPermissionsStatics;
	hr = GetActivationFactory(
		HStringReference(RuntimeClass_Windows_Storage_AccessCache_StorageApplicationPermissions).Get(),
		&storageApplicationPermissionsStatics);

	ComPtr<IStorageItemAccessList> storageItemAccessList;
	hr = storageApplicationPermissionsStatics->get_FutureAccessList(&storageItemAccessList);

	ComPtr<IStorageFolder> storageFolder;
	HStringReference token(L"XASH3D_BASEDIR");
	do
	{
		ComPtr<IAsyncOperation<StorageFolder*>> futureStorageFolder;
		hr = storageItemAccessList->GetFolderAsync(token.Get(), &futureStorageFolder);
		if (futureStorageFolder)
		{
			WinRT_SDL_Await(futureStorageFolder);
			hr = futureStorageFolder->GetResults(&storageFolder);
			break;
		}

		ComPtr<IFolderPicker> folderPicker;
		hr = ActivateInstance(
			HStringReference(RuntimeClass_Windows_Storage_Pickers_FolderPicker).Get(),
			&folderPicker);

		folderPicker->put_SuggestedStartLocation(PickerLocationId_Desktop);

		ComPtr<IVector<HSTRING>> fileTypeFilterRef;
		hr = folderPicker->get_FileTypeFilter(&fileTypeFilterRef);
		HStringReference star(L"*");
		hr = fileTypeFilterRef->Append(star.Get());

		SDL_CaptureMouse(SDL_FALSE);
		SDL_SetRelativeMouseMode(SDL_FALSE);

		WinRT_SDL_PushStatus();
		hr = folderPicker->PickSingleFolderAsync(&futureStorageFolder);
		
		if (futureStorageFolder)
		{
			WinRT_SDL_Await(futureStorageFolder);
			hr = futureStorageFolder->GetResults(&storageFolder);
			break;
		}
		WinRT_SDL_PopStatus();
	} while (0);

	if (!storageFolder)
		return s_szWinRTGameDIR;

	ComPtr<IStorageItem> storageFolderItem;
	hr = storageFolder.As<IStorageItem>(&storageFolderItem);

	storageItemAccessList->AddOrReplaceOverloadDefaultMetadata(token.Get(), storageFolderItem.Get());

	HSTRING hSTRPath;
	hr = storageFolderItem->get_Path(&hSTRPath);
	HString hStrPath;
	hStrPath.Attach(hSTRPath);

	unsigned len = 0;
	const wchar_t* wstr = hStrPath.GetRawBuffer(&len);

	WideCharToMultiByte(CP_ACP, 0, wstr, -1, s_szWinRTGameDIR, 1024, NULL, FALSE);

	return s_szWinRTGameDIR;
}
#endif

const char* WinRT_GetGameFolder()
{
	return WinRT_GetGameFolderAppData();
}
