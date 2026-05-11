"GameMenu"
{
	"Background"
	{
		"ControlName" "Panel"
		"xpos" "0"
		"ypos" "0"
		"wide" "640"
		"tall" "480"
		"bgcolor" "8 12 16 220"
	}

	"Title"
	{
		"ControlName" "Label"
		"text" "Xash3D VGUI"
		"textAlignment" "center"
		"xpos" "c-160"
		"ypos" "72"
		"wide" "320"
		"tall" "32"
		"fgcolor" "255 220 160 255"
		"paintbackground" "0"
	}

	"ResumeButton"
	{
		"ControlName" "Button"
		"labelText" "Resume"
		"command" "CloseMenu"
		"xpos" "c-110"
		"ypos" "148"
		"wide" "220"
		"tall" "36"
	}

	"ServersButton"
	{
		"ControlName" "Button"
		"labelText" "Servers"
		"command" "OpenServers"
		"xpos" "c-110"
		"ypos" "194"
		"wide" "220"
		"tall" "36"
	}

	"OptionsButton"
	{
		"ControlName" "Button"
		"labelText" "Options"
		"command" "togglemenu"
		"xpos" "c-110"
		"ypos" "240"
		"wide" "220"
		"tall" "36"
	}

	"ConsoleButton"
	{
		"ControlName" "Button"
		"labelText" "Console"
		"command" "toggleconsole"
		"xpos" "c-110"
		"ypos" "286"
		"wide" "220"
		"tall" "36"
	}

	"MOTDButton"
	{
		"ControlName" "Button"
		"labelText" "MOTD"
		"command" "ShowMOTD"
		"xpos" "c-110"
		"ypos" "332"
		"wide" "220"
		"tall" "36"
	}

	"QuitButton"
	{
		"ControlName" "Button"
		"labelText" "Quit"
		"command" "quit"
		"xpos" "c-110"
		"ypos" "378"
		"wide" "220"
		"tall" "36"
	}
}
