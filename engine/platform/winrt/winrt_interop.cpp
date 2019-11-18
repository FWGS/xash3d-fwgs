
#include <wrl.h>
#include <windows.ui.ViewManagement.h>
#include <windows.applicationmodel.core.h>
#include <windows.graphics.display.h>
#include <windows.System.h>

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
using namespace ABI::Windows::UI;
using namespace ABI::Windows::UI::Core;
using namespace ABI::Windows::UI::ViewManagement;
using namespace ABI::Windows::ApplicationModel::Core;
using namespace ABI::Windows::Graphics::Display;

class ColorReference : public RuntimeClass<RuntimeClassFlags<WinRt>, __FIReference_1_Windows__CUI__CColor>
{
public:
	HRESULT get_Value(Color* value) override { return *value = color, S_OK; }
	Color color;
private:
	~ColorReference() {}
};

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
