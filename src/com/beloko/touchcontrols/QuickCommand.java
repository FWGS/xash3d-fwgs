package com.beloko.touchcontrols;

import java.io.Serializable;

public class QuickCommand implements Serializable{
	/**
	 * 
	 */
	private static final long serialVersionUID = 1L;
	
	String title;
	String command;
	
	QuickCommand(String title, String command)
	{
		this.title = title;
		this.command = command;
	}

	public String getTitle() {
		return title;
	}

	public void setTitle(String title) {
		this.title = title;
	}

	public String getCommand() {
		return command;
	}

	public void setCommand(String command) {
		this.command = command;
	}
	
}
