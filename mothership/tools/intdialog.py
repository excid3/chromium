# Copyright (c) 2001, Stanford University
# All rights reserved.
#
# See the file LICENSE.txt for information on redistributing this software.
#
# Authors:
#   Brian Paul


"""The IntDialog class defines a dialog for entering one or more integer
values with standard OK / Cancel buttons.
"""

from wxPython.wx import *


class IntDialog(wxDialog):
	def __init__(self, parent, id, title, labels=["Enter Integer"],
				 defaultValues=[1], minValue=0, maxValue=100):
		"""parent, id, and title are the standard wxDialog parameters.
		labels is an array of the message to display.  One integer spin
		control will be made for each message.
		defaultValue, minValue, maxValue are self-explanatory.
		"""
		wxDialog.__init__(self, parent, id, title, pos=wxPoint(-1,-1),
						  style = wxDEFAULT_DIALOG_STYLE|wxRESIZE_BORDER)

		id_OK = 1
		id_CANCEL = 2
		id_SPINNER = 3
		self.spinners = []

		outerSizer = wxBoxSizer(wxVERTICAL)

		# grid sizer for labels/spinners
		flexSizer = wxFlexGridSizer(cols=2, hgap=4, vgap=4)
		i = 0
		for lab in labels:
			textLabel = wxStaticText(parent=self, id=-1, label=lab + "  ")
			flexSizer.Add(textLabel, 0, wxALIGN_LEFT|wxALIGN_CENTER_VERTICAL)
			spinner = wxSpinCtrl(parent=self, id=id_SPINNER,
								 min=minValue, max=maxValue,
								 value=str(defaultValues[i]))
			flexSizer.Add(spinner, 0, wxALIGN_CENTER)
			self.spinners.append(spinner)
			i += 1
		outerSizer.Add(flexSizer, 1, wxALL|wxGROW, 10)

		# horizontal separator (box with height=0)
		box = wxStaticBox(parent=self, id=-1, label="", size=wxSize(100,0))
		outerSizer.Add(box, flag=wxGROW|wxBOTTOM, border=8)

		# sizer for OK, Cancel buttons
		rowSizer = wxGridSizer(rows=1, cols=2, vgap=4, hgap=20)
		self.OkButton = wxButton(parent=self, id=id_OK, label="OK")
		rowSizer.Add(self.OkButton, 0, wxALIGN_CENTER, 10)
		self.CancelButton = wxButton(parent=self, id=id_CANCEL, label="Cancel")
		rowSizer.Add(self.CancelButton, 0, wxALIGN_CENTER, 10)
		outerSizer.Add(rowSizer, 0, wxGROW|wxBOTTOM|wxLEFT|wxRIGHT, 8)
		EVT_BUTTON(self.OkButton, id_OK, self._onOK)
		EVT_BUTTON(self.CancelButton, id_CANCEL, self._onCancel)

		min = outerSizer.GetMinSize()
		self.SetSizer(outerSizer)
		self.SetAutoLayout(true)
		self.SetSizeHints(minW=min[0], minH=min[1])
		self.SetSize(min)

	def _onOK(self, event):
		"""Called by OK button"""
		self.EndModal(wxID_OK)

	def _onCancel(self, event):
		"""Called by Cancel button"""
		self.EndModal(wxID_CANCEL)

	def GetValue(self):
		"""Return value of the first spinner."""
		return self.spinners[0].GetValue()

	def GetValues(self):
		"""Return array of current integer values"""
		retVal = []
		for s in self.spinners:
			retVal.append(int(s.GetValue()))
		return retVal

	# Override the wxDialog.ShowModal() method
	def ShowModal(self):
		"""Display dialog and return wxID_OK or wxID_CANCEL."""
		retVal = wxDialog.ShowModal(self)
		return retVal


# ======================================================================
# Test routines

class TestFrame(wxFrame):
	def __init__(self, parent, id, title):
		wxFrame.__init__(self, parent, id, title,
				 style = wxDEFAULT_FRAME_STYLE | wxWANTS_CHARS |
					 wxNO_FULL_REPAINT_ON_RESIZE)
		EVT_CLOSE(self, self.doClose)

	def doClose(self, event):
		global app
		self.Destroy()
		app.ExitMainLoop()
	
class TestApp(wxApp):
	""" Test harness wxApp class."""
	def OnInit(self):
		wxInitAllImageHandlers()

		frame = TestFrame(parent=None, id=-1, title="Test App")
		frame.Show(TRUE)

		dialog = IntDialog(parent=frame, id=-1, title="Test Dialog",
						   labels=["Value 1", "Value 2"],
						   defaultValues=[5, 10])
		dialog.Centre()
		dialog.ShowModal()
		return TRUE

def _test():
	global app
	app = TestApp()
	app.MainLoop()
	return

if __name__ == "__main__":
	_test()
