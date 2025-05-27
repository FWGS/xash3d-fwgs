#include "VrInput.h"
#include "VrMath.h"
#include <string.h>

//OpenXR
XrPath leftHandPath;
XrPath rightHandPath;
XrAction handPoseLeftAction;
XrAction handPoseRightAction;
XrAction indexLeftAction;
XrAction indexRightAction;
XrAction menuAction;
XrAction buttonAAction;
XrAction buttonBAction;
XrAction buttonXAction;
XrAction buttonYAction;
XrAction gripLeftAction;
XrAction gripRightAction;
XrAction moveOnLeftJoystickAction;
XrAction moveOnRightJoystickAction;
XrAction thumbstickLeftClickAction;
XrAction thumbstickRightClickAction;
XrAction vibrateLeftFeedback;
XrAction vibrateRightFeedback;
XrActionSet runningActionSet;
XrSpace leftControllerAimSpace = XR_NULL_HANDLE;
XrSpace rightControllerAimSpace = XR_NULL_HANDLE;
int inputInitialized = 0;

bool lActive = false;
bool rActive = false;
uint32_t lButtons = 0;
uint32_t rButtons = 0;
XrActionStateVector2f moveJoystickState[2];

void INVR_Vibrate( float duration, int channel, float intensity ) {
	if (intensity > 0) {
		XrHapticVibration vibration = {};
		vibration.type = XR_TYPE_HAPTIC_VIBRATION;
		vibration.next = NULL;
		vibration.amplitude = intensity;
		vibration.duration = ToXrTime(duration);
		vibration.frequency = 300;
		XrHapticActionInfo hapticActionInfo = {};
		hapticActionInfo.type = XR_TYPE_HAPTIC_ACTION_INFO;
		hapticActionInfo.next = NULL;
		hapticActionInfo.action = channel == 0 ? vibrateLeftFeedback : vibrateRightFeedback;
		OXR(xrApplyHapticFeedback(VR_GetEngine()->appState.Session, &hapticActionInfo, (const XrHapticBaseHeader*)&vibration));
	} else {
		// Stop haptics
		XrHapticActionInfo hapticActionInfo = {};
		hapticActionInfo.type = XR_TYPE_HAPTIC_ACTION_INFO;
		hapticActionInfo.next = NULL;
		hapticActionInfo.action = channel == 0 ? vibrateLeftFeedback : vibrateRightFeedback;
		OXR(xrStopHapticFeedback(VR_GetEngine()->appState.Session, &hapticActionInfo));
	}
}

XrSpace CreateActionSpace(XrAction poseAction, XrPath subactionPath) {
	XrActionSpaceCreateInfo asci = {};
	asci.type = XR_TYPE_ACTION_SPACE_CREATE_INFO;
	asci.action = poseAction;
	asci.poseInActionSpace.orientation.w = 1.0f;
	asci.subactionPath = subactionPath;
	XrSpace actionSpace = XR_NULL_HANDLE;
	OXR(xrCreateActionSpace(VR_GetEngine()->appState.Session, &asci, &actionSpace));
	return actionSpace;
}

XrActionSuggestedBinding ActionSuggestedBinding(XrAction action, const char* bindingString) {
	XrActionSuggestedBinding asb;
	asb.action = action;
	XrPath bindingPath;
	OXR(xrStringToPath(VR_GetEngine()->appState.Instance, bindingString, &bindingPath));
	asb.binding = bindingPath;
	return asb;
}

XrActionSet CreateActionSet(int priority, const char* name, const char* localizedName) {
	XrActionSetCreateInfo asci = {};
	asci.type = XR_TYPE_ACTION_SET_CREATE_INFO;
	asci.next = NULL;
	asci.priority = priority;
	strcpy(asci.actionSetName, name);
	strcpy(asci.localizedActionSetName, localizedName);
	XrActionSet actionSet = XR_NULL_HANDLE;
	OXR(xrCreateActionSet(VR_GetEngine()->appState.Instance, &asci, &actionSet));
	return actionSet;
}

XrAction CreateAction(
		XrActionSet actionSet,
		XrActionType type,
		const char* actionName,
		const char* localizedName,
		int countSubactionPaths,
		XrPath* subactionPaths) {

	XrActionCreateInfo aci = {};
	aci.type = XR_TYPE_ACTION_CREATE_INFO;
	aci.next = NULL;
	aci.actionType = type;
	if (countSubactionPaths > 0) {
		aci.countSubactionPaths = countSubactionPaths;
		aci.subactionPaths = subactionPaths;
	}
	strcpy(aci.actionName, actionName);
	strcpy(aci.localizedActionName, localizedName ? localizedName : actionName);
	XrAction action = XR_NULL_HANDLE;
	OXR(xrCreateAction(actionSet, &aci, &action));
	return action;
}

int ActionPoseIsActive(XrAction action, XrPath subactionPath) {
	XrActionStateGetInfo getInfo = {};
	getInfo.type = XR_TYPE_ACTION_STATE_GET_INFO;
	getInfo.action = action;
	getInfo.subactionPath = subactionPath;

	XrActionStatePose state = {};
	state.type = XR_TYPE_ACTION_STATE_POSE;
	OXR(xrGetActionStatePose(VR_GetEngine()->appState.Session, &getInfo, &state));
	return state.isActive != XR_FALSE;
}

XrActionStateFloat GetActionStateFloat(XrAction action) {
	XrActionStateGetInfo getInfo = {};
	getInfo.type = XR_TYPE_ACTION_STATE_GET_INFO;
	getInfo.action = action;

	XrActionStateFloat state = {};
	state.type = XR_TYPE_ACTION_STATE_FLOAT;

	OXR(xrGetActionStateFloat(VR_GetEngine()->appState.Session, &getInfo, &state));
	return state;
}

XrActionStateBoolean GetActionStateBoolean(XrAction action) {
	XrActionStateGetInfo getInfo = {};
	getInfo.type = XR_TYPE_ACTION_STATE_GET_INFO;
	getInfo.action = action;

	XrActionStateBoolean state = {};
	state.type = XR_TYPE_ACTION_STATE_BOOLEAN;

	OXR(xrGetActionStateBoolean(VR_GetEngine()->appState.Session, &getInfo, &state));
	return state;
}

XrActionStateVector2f GetActionStateVector2(XrAction action) {
	XrActionStateGetInfo getInfo = {};
	getInfo.type = XR_TYPE_ACTION_STATE_GET_INFO;
	getInfo.action = action;

	XrActionStateVector2f state = {};
	state.type = XR_TYPE_ACTION_STATE_VECTOR2F;

	OXR(xrGetActionStateVector2f(VR_GetEngine()->appState.Session, &getInfo, &state));
	return state;
}

void IN_VRInit( engine_t *engine ) {
	if (inputInitialized)
		return;

	// Actions
	runningActionSet = CreateActionSet(1, "running_action_set", "Action Set used on main loop");
	indexLeftAction = CreateAction(runningActionSet, XR_ACTION_TYPE_BOOLEAN_INPUT, "index_left", "Index left", 0, NULL);
	indexRightAction = CreateAction(runningActionSet, XR_ACTION_TYPE_BOOLEAN_INPUT, "index_right", "Index right", 0, NULL);
	menuAction = CreateAction(runningActionSet, XR_ACTION_TYPE_BOOLEAN_INPUT, "menu_action", "Menu", 0, NULL);
	buttonAAction = CreateAction(runningActionSet, XR_ACTION_TYPE_BOOLEAN_INPUT, "button_a", "Button A", 0, NULL);
	buttonBAction = CreateAction(runningActionSet, XR_ACTION_TYPE_BOOLEAN_INPUT, "button_b", "Button B", 0, NULL);
	buttonXAction = CreateAction(runningActionSet, XR_ACTION_TYPE_BOOLEAN_INPUT, "button_x", "Button X", 0, NULL);
	buttonYAction = CreateAction(runningActionSet, XR_ACTION_TYPE_BOOLEAN_INPUT, "button_y", "Button Y", 0, NULL);
	gripLeftAction = CreateAction(runningActionSet, XR_ACTION_TYPE_FLOAT_INPUT, "grip_left", "Grip left", 0, NULL);
	gripRightAction = CreateAction(runningActionSet, XR_ACTION_TYPE_FLOAT_INPUT, "grip_right", "Grip right", 0, NULL);
	moveOnLeftJoystickAction = CreateAction(runningActionSet, XR_ACTION_TYPE_VECTOR2F_INPUT, "move_on_left_joy", "Move on left Joy", 0, NULL);
	moveOnRightJoystickAction = CreateAction(runningActionSet, XR_ACTION_TYPE_VECTOR2F_INPUT, "move_on_right_joy", "Move on right Joy", 0, NULL);
	thumbstickLeftClickAction = CreateAction(runningActionSet, XR_ACTION_TYPE_BOOLEAN_INPUT, "thumbstick_left", "Thumbstick left", 0, NULL);
	thumbstickRightClickAction = CreateAction(runningActionSet, XR_ACTION_TYPE_BOOLEAN_INPUT, "thumbstick_right", "Thumbstick right", 0, NULL);
	vibrateLeftFeedback = CreateAction(runningActionSet, XR_ACTION_TYPE_VIBRATION_OUTPUT, "vibrate_left_feedback", "Vibrate Left Controller Feedback", 0, NULL);
	vibrateRightFeedback = CreateAction(runningActionSet, XR_ACTION_TYPE_VIBRATION_OUTPUT, "vibrate_right_feedback", "Vibrate Right Controller Feedback", 0, NULL);

	OXR(xrStringToPath(engine->appState.Instance, "/user/hand/left", &leftHandPath));
	OXR(xrStringToPath(engine->appState.Instance, "/user/hand/right", &rightHandPath));
	handPoseLeftAction = CreateAction(runningActionSet, XR_ACTION_TYPE_POSE_INPUT, "hand_pose_left", NULL, 1, &leftHandPath);
	handPoseRightAction = CreateAction(runningActionSet, XR_ACTION_TYPE_POSE_INPUT, "hand_pose_right", NULL, 1, &rightHandPath);

	XrPath interactionProfilePath = XR_NULL_PATH;
	if (VR_GetPlatformFlag(VR_PLATFORM_CONTROLLER_QUEST)) {
		OXR(xrStringToPath(engine->appState.Instance, "/interaction_profiles/oculus/touch_controller", &interactionProfilePath));
	} else if (VR_GetPlatformFlag(VR_PLATFORM_CONTROLLER_PICO)) {
		OXR(xrStringToPath(engine->appState.Instance, "/interaction_profiles/pico/neo3_controller", &interactionProfilePath));
	}

	// Map bindings
	XrActionSuggestedBinding bindings[32]; // large enough for all profiles
	int currBinding = 0;


	if (VR_GetPlatformFlag(VR_PLATFORM_CONTROLLER_QUEST)) {
		bindings[currBinding++] = ActionSuggestedBinding(indexLeftAction, "/user/hand/left/input/trigger");
		bindings[currBinding++] = ActionSuggestedBinding(indexRightAction, "/user/hand/right/input/trigger");
		bindings[currBinding++] = ActionSuggestedBinding(menuAction, "/user/hand/left/input/menu/click");
	} else if (VR_GetPlatformFlag(VR_PLATFORM_CONTROLLER_PICO)) {
		bindings[currBinding++] = ActionSuggestedBinding(indexLeftAction,  "/user/hand/left/input/trigger/click");
		bindings[currBinding++] = ActionSuggestedBinding(indexRightAction,  "/user/hand/right/input/trigger/click");
		bindings[currBinding++] = ActionSuggestedBinding(menuAction, "/user/hand/left/input/back/click");
		bindings[currBinding++] = ActionSuggestedBinding(menuAction, "/user/hand/right/input/back/click");
	}
	bindings[currBinding++] = ActionSuggestedBinding(buttonXAction, "/user/hand/left/input/x/click");
	bindings[currBinding++] = ActionSuggestedBinding(buttonYAction, "/user/hand/left/input/y/click");
	bindings[currBinding++] = ActionSuggestedBinding(buttonAAction, "/user/hand/right/input/a/click");
	bindings[currBinding++] = ActionSuggestedBinding(buttonBAction, "/user/hand/right/input/b/click");
	bindings[currBinding++] = ActionSuggestedBinding(gripLeftAction, "/user/hand/left/input/squeeze/value");
	bindings[currBinding++] = ActionSuggestedBinding(gripRightAction, "/user/hand/right/input/squeeze/value");
	bindings[currBinding++] = ActionSuggestedBinding(moveOnLeftJoystickAction, "/user/hand/left/input/thumbstick");
	bindings[currBinding++] = ActionSuggestedBinding(moveOnRightJoystickAction, "/user/hand/right/input/thumbstick");
	bindings[currBinding++] = ActionSuggestedBinding(thumbstickLeftClickAction, "/user/hand/left/input/thumbstick/click");
	bindings[currBinding++] = ActionSuggestedBinding(thumbstickRightClickAction, "/user/hand/right/input/thumbstick/click");
	bindings[currBinding++] = ActionSuggestedBinding(vibrateLeftFeedback, "/user/hand/left/output/haptic");
	bindings[currBinding++] = ActionSuggestedBinding(vibrateRightFeedback, "/user/hand/right/output/haptic");
	bindings[currBinding++] = ActionSuggestedBinding(handPoseLeftAction, "/user/hand/left/input/aim/pose");
	bindings[currBinding++] = ActionSuggestedBinding(handPoseRightAction, "/user/hand/right/input/aim/pose");

	XrInteractionProfileSuggestedBinding suggestedBindings = {};
	suggestedBindings.type = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING;
	suggestedBindings.next = NULL;
	suggestedBindings.interactionProfile = interactionProfilePath;
	suggestedBindings.suggestedBindings = bindings;
	suggestedBindings.countSuggestedBindings = currBinding;
	OXR(xrSuggestInteractionProfileBindings(engine->appState.Instance, &suggestedBindings));

	// Attach actions
	XrSessionActionSetsAttachInfo attachInfo = {};
	attachInfo.type = XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO;
	attachInfo.next = NULL;
	attachInfo.countActionSets = 1;
	attachInfo.actionSets = &runningActionSet;
	OXR(xrAttachSessionActionSets(engine->appState.Session, &attachInfo));

	// Enumerate actions
	XrPath actionPathsBuffer[32];
	char stringBuffer[256];
	XrAction actionsToEnumerate[] = {
			indexLeftAction,
			indexRightAction,
			menuAction,
			buttonAAction,
			buttonBAction,
			buttonXAction,
			buttonYAction,
			gripLeftAction,
			gripRightAction,
			moveOnLeftJoystickAction,
			moveOnRightJoystickAction,
			thumbstickLeftClickAction,
			thumbstickRightClickAction,
			vibrateLeftFeedback,
			vibrateRightFeedback,
			handPoseLeftAction,
			handPoseRightAction
	};
	for (int index = 0; index < sizeof(actionsToEnumerate) / sizeof(XrAction); index++) {
		XrBoundSourcesForActionEnumerateInfo enumerateInfo = {};
		enumerateInfo.type = XR_TYPE_BOUND_SOURCES_FOR_ACTION_ENUMERATE_INFO;
		enumerateInfo.next = NULL;
		enumerateInfo.action = actionsToEnumerate[index];

		// Get Count
		uint32_t countOutput = 0;
		OXR(xrEnumerateBoundSourcesForAction(engine->appState.Session, &enumerateInfo, 0 /* request size */, &countOutput, NULL));
		ALOGV("xrEnumerateBoundSourcesForAction action=%lld count=%u", (long long)enumerateInfo.action, countOutput);

		if (countOutput < 32) {
			OXR(xrEnumerateBoundSourcesForAction(
					engine->appState.Session, &enumerateInfo, 32, &countOutput, actionPathsBuffer));
			for (uint32_t a = 0; a < countOutput; ++a) {
				XrInputSourceLocalizedNameGetInfo nameGetInfo = {};
				nameGetInfo.type = XR_TYPE_INPUT_SOURCE_LOCALIZED_NAME_GET_INFO;
				nameGetInfo.next = NULL;
				nameGetInfo.sourcePath = actionPathsBuffer[a];
				nameGetInfo.whichComponents = XR_INPUT_SOURCE_LOCALIZED_NAME_USER_PATH_BIT |
				                              XR_INPUT_SOURCE_LOCALIZED_NAME_INTERACTION_PROFILE_BIT |
				                              XR_INPUT_SOURCE_LOCALIZED_NAME_COMPONENT_BIT;

				uint32_t stringCount = 0u;
				OXR(xrGetInputSourceLocalizedName(engine->appState.Session, &nameGetInfo, 0, &stringCount, NULL));
				if (stringCount < 256) {
					OXR(xrGetInputSourceLocalizedName(engine->appState.Session, &nameGetInfo, 256, &stringCount, stringBuffer));
					char pathStr[256];
					uint32_t strLen = 0;
					OXR(xrPathToString(engine->appState.Instance, actionPathsBuffer[a], (uint32_t)sizeof(pathStr), &strLen, pathStr));
					ALOGV("  -> path = %lld `%s` -> `%s`", (long long)actionPathsBuffer[a], pathStr, stringBuffer);
				}
			}
		}
	}
	inputInitialized = 1;
}

void IN_VRInputFrame( engine_t* engine ) {

	// sync action data
	XrActiveActionSet activeActionSet = {};
	activeActionSet.actionSet = runningActionSet;
	activeActionSet.subactionPath = XR_NULL_PATH;

	XrActionsSyncInfo syncInfo = {};
	syncInfo.type = XR_TYPE_ACTIONS_SYNC_INFO;
	syncInfo.next = NULL;
	syncInfo.countActiveActionSets = 1;
	syncInfo.activeActionSets = &activeActionSet;
	OXR(xrSyncActions(engine->appState.Session, &syncInfo));

	// query input action states
	XrActionStateGetInfo getInfo = {};
	getInfo.type = XR_TYPE_ACTION_STATE_GET_INFO;
	getInfo.next = NULL;
	getInfo.subactionPath = XR_NULL_PATH;

	if (leftControllerAimSpace == XR_NULL_HANDLE) {
		leftControllerAimSpace = CreateActionSpace(handPoseLeftAction, leftHandPath);
	}
	if (rightControllerAimSpace == XR_NULL_HANDLE) {
		rightControllerAimSpace = CreateActionSpace(handPoseRightAction, rightHandPath);
	}

	//button mapping
	lButtons = 0;
	if (GetActionStateBoolean(menuAction).currentState) lButtons |= ovrButton_Enter;
	if (GetActionStateBoolean(buttonXAction).currentState) lButtons |= ovrButton_X;
	if (GetActionStateBoolean(buttonYAction).currentState) lButtons |= ovrButton_Y;
	if (GetActionStateBoolean(indexLeftAction).currentState) lButtons |= ovrButton_Trigger;
	if (GetActionStateFloat(gripLeftAction).currentState > 0.5f) lButtons |= ovrButton_GripTrigger;
	if (GetActionStateBoolean(thumbstickLeftClickAction).currentState) lButtons |= ovrButton_Joystick;
	rButtons = 0;
	if (GetActionStateBoolean(buttonAAction).currentState) rButtons |= ovrButton_A;
	if (GetActionStateBoolean(buttonBAction).currentState) rButtons |= ovrButton_B;
	if (GetActionStateBoolean(indexRightAction).currentState) rButtons |= ovrButton_Trigger;
	if (GetActionStateFloat(gripRightAction).currentState > 0.5f) rButtons |= ovrButton_GripTrigger;
	if (GetActionStateBoolean(thumbstickRightClickAction).currentState) rButtons |= ovrButton_Joystick;

	//thumbstick
	moveJoystickState[0] = GetActionStateVector2(moveOnLeftJoystickAction);
	moveJoystickState[1] = GetActionStateVector2(moveOnRightJoystickAction);
	if (moveJoystickState[0].currentState.x > 0.5) lButtons |= ovrButton_Right;
	if (moveJoystickState[0].currentState.x < -0.5) lButtons |= ovrButton_Left;
	if (moveJoystickState[0].currentState.y > 0.5) lButtons |= ovrButton_Up;
	if (moveJoystickState[0].currentState.y < -0.5) lButtons |= ovrButton_Down;
	if (moveJoystickState[1].currentState.x > 0.5) rButtons |= ovrButton_Right;
	if (moveJoystickState[1].currentState.x < -0.5) rButtons |= ovrButton_Left;
	if (moveJoystickState[1].currentState.y > 0.5) rButtons |= ovrButton_Up;
	if (moveJoystickState[1].currentState.y < -0.5) rButtons |= ovrButton_Down;

	//check if controller is active
	lActive = moveJoystickState[0].isActive;
	rActive = moveJoystickState[1].isActive;
}


uint32_t IN_VRGetButtonState( int controllerIndex ) {
	switch (controllerIndex) {
		case 0:
			return lButtons;
		case 1:
			return rButtons;
		default:
			return 0;
	}
}

XrVector2f IN_VRGetJoystickState( int controllerIndex ) {
	return moveJoystickState[controllerIndex].currentState;
}

XrPosef IN_VRGetPose( int controllerIndex ) {
	engine_t* engine = VR_GetEngine();
	XrSpaceLocation loc = {};
	loc.type = XR_TYPE_SPACE_LOCATION;
	XrSpace aimSpace[] = { leftControllerAimSpace, rightControllerAimSpace };
	xrLocateSpace(aimSpace[controllerIndex], engine->appState.CurrentSpace, (XrTime)(engine->predictedDisplayTime), &loc);
	return loc.pose;
}

bool IN_VRIsActive( int controllerIndex ) {
	switch (controllerIndex) {
		case 0:
			return lActive;
		case 1:
			return rActive;
		default:
			return false;
	}
}
