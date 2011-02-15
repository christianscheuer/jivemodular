/*
 ==============================================================================

 This file is part of the JUCETICE project - Copyright 2007 by Lucio Asnaghi.

 JUCETICE is based around the JUCE library - "Jules' Utility Class Extensions"
 Copyright 2007 by Julian Storer.

 ------------------------------------------------------------------------------

 JUCE and JUCETICE can be redistributed and/or modified under the terms of
 the GNU Lesser General Public License, as published by the Free Software
 Foundation; either version 2 of the License, or (at your option) any later
 version.

 JUCE and JUCETICE are distributed in the hope that they will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU Lesser General Public License
 along with JUCE and JUCETICE; if not, visit www.gnu.org/licenses or write to
 Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 Boston, MA 02111-1307 USA

 ==============================================================================
*/

#include "SequenceComponent.h"
#include "ui/utility/JiveClipListComponent.h"

//==============================================================================
// Subcomponent for the note/pianoroll editor, comprising the note grid and the piano keyboard.
// In future may also contain a left pane for note editing controls (e.g. velocity etc).
class NoteEditComponent : public Component
{
public:
	NoteEditComponent(MidiSequencePlugin* _plugin, Transport* _transport);
	~NoteEditComponent();

	PianoGridKeyboard* getMidiKeyboard() const { return midiKeyboard; };
	PianoGrid* getPianoGrid() const { return pianoGrid; };
	
    void  setGridLeftX (int x)               { gridLeftX = x; }
    int getGridLeftX () const               { return gridLeftX; }

	void resized();

    int getMidiKeyboardWidth () const               { return 52; }

   ComboBox* getNoteLengthBox() {return noteLengthBox;};
protected:
    MidiSequencePlugin* plugin;
    Transport* transport;

    Label* noteLengthLabel;
    ComboBox* noteLengthBox;

    PianoGrid* pianoGrid;
    Viewport* pianoViewport;

    DropShadower* dropShadower;
    PianoGridKeyboard* midiKeyboard;
	
	int gridLeftX;
};

NoteEditComponent::NoteEditComponent(MidiSequencePlugin* _plugin, Transport* _transport)
:
   plugin(_plugin),
   transport(_transport),
   pianoGrid (0),
   pianoViewport (0),
   dropShadower (0),
   midiKeyboard (0),
   gridLeftX (120),
   noteLengthLabel(0),
   noteLengthBox(0)
{
   // note length combobox
   addAndMakeVisible (noteLengthLabel = new Label ("NoteLabel", "Note:"));
   addAndMakeVisible (noteLengthBox = new ComboBox (String::empty));
   noteLengthBox->setEditableText (false);
   noteLengthBox->setTooltip (T("Note length"));
   noteLengthBox->setJustificationType (Justification::centredLeft);
   noteLengthBox->addItem (T("64th"), 64);
   noteLengthBox->addItem (T("32th"), 32);
   noteLengthBox->addItem (T("16th"), 16);
   noteLengthBox->addItem (T("8th"), 8);
   noteLengthBox->addItem (T("beat"), 4);
   noteLengthBox->addItem (T("1/2 bar"), 2);
   noteLengthBox->addItem (T("1 bar"), 1);
   noteLengthBox->setTooltip (T("Note length"));
//   noteLengthBox->addListener (this);
   noteLengthBox->setSelectedId (plugin->getIntValue (PROP_SEQNOTELENGTH, 4));

   // grid
   pianoGrid = new PianoGrid ();
   pianoGrid->setTimeDivision (transport->getTimeDenominator());
   pianoGrid->setSnapQuantize (plugin->getIntValue (PROP_SEQNOTESNAP, 4));
   pianoGrid->setNumBars (plugin->getIntValue (PROP_SEQBAR, transport->getNumBars()));
   pianoGrid->setBarWidth (plugin->getIntValue (PROP_SEQCOLSIZE, 80));
   pianoGrid->setRowsOffset (plugin->getIntValue (PROP_SEQROWOFFSET, 0));
   pianoGrid->setListener (plugin);

   addAndMakeVisible (pianoViewport = new Viewport (String::empty));
   pianoViewport->setScrollBarsShown (false, true);
   pianoViewport->setScrollBarThickness (12);
   pianoViewport->setViewedComponent (pianoGrid);

   // midi keyboard
   addAndMakeVisible (midiKeyboard = new PianoGridKeyboard (plugin->getKeyboardState ()));
   midiKeyboard->setMidiChannel (1);
   midiKeyboard->setMidiChannelsToDisplay (1 << 0);
   midiKeyboard->setKeyPressBaseOctave (3);
   midiKeyboard->setLowestVisibleKey (pianoGrid->getRowsOffset());
   midiKeyboard->setKeyWidth (12); // fixed !
   midiKeyboard->setAvailableRange (0, 119);
   midiKeyboard->setScrollButtonsVisible (true);

   dropShadower = new DropShadower (0.4f, 2, 0, 2.5f);
   dropShadower->setOwner (midiKeyboard);
}

NoteEditComponent::~NoteEditComponent()
{
    deleteAndZero (dropShadower);
	deleteAllChildren ();
}

void NoteEditComponent::resized ()
{
    int keysWidth = getMidiKeyboardWidth ();
    int keysButtonHeight = 12;

    // content
    noteLengthLabel->setBounds (0, 0, 60, 16);
    noteLengthBox->setBounds (0, 16, 60, 16);

    midiKeyboard->setBounds (gridLeftX - keysWidth, 0, keysWidth, getHeight());
    pianoViewport->setBounds (gridLeftX,
                              0,
                              getWidth () - gridLeftX,
                              getHeight() - keysButtonHeight);

    pianoGrid->setSize (300, getHeight() - keysButtonHeight);
    pianoGrid->updateSize ();
}

//==============================================================================
// Subcomponent for CC automation editor, comprising an automation canvas and a listbox showing the controllers that can be automated. 
// Multiple CC "lanes" can be edited, each has a CC number, name and a colour (these are not user-editable at present).
// One CC lane is "current" at a given time; mouse clicks will add events for that lane, and selection will only include events for that lane.
// The current CC lane is specified via the listbox, and is indicated in the canvas by bolder events joined with a thicker line.
class AutomationEditComponent : public Component, public ListBoxModel
{
public:
	AutomationEditComponent(MidiSequencePlugin* _plugin, Transport* _transport);
	~AutomationEditComponent();

	//==================================================
	// Manage the controllers to edit via this component.
	void addCC();
	void deleteCurrentCC(bool notifyListener=true);
	void clearCCs(bool notifyListener=true);
	void setCurrentCC(int controller = -1);
	int findIndexOfCC(int controller); // returns the row index for the CC, or -1 if not found
	
	//==================================================
	Colour GetNextControllerColour();
	int GetNextControllerNumber();
	
	//==================================================
	void addAutomationEvent(int controller, double value, double beat);

	//==================================================
	// ListBoxModel interface
    virtual int getNumRows();
	virtual void paintListBoxItem (int rowNumber,
                                   Graphics& g,
                                   int width, int height,
                                   bool rowIsSelected);
	void selectedRowsChanged (int lastRowSelected);

	//==================================================
    void  setGridLeftX (int x)               { gridLeftX = x; }
    int getGridLeftX () const               { return gridLeftX; }	

	//==================================================
	AutomationGrid* getAutomationGrid() const { return automationGrid; };

	//==================================================
	void resized();

protected:
    MidiSequencePlugin* plugin;
    Transport* transport;

    AutomationGrid* automationGrid;
    Viewport* automationViewport;
	ListBox* leftPane;
	
	// Represents a controller lane being edited in an AutomationEditComponent, with a name, CC number, and a colour.
	class AutomatableController
	{
	public:
		AutomatableController() {};
		AutomatableController(int _number, String _name = String("CC"), Colour _colour = Colour())
		: 
			number(_number), 
			name(_name),
			colour(_colour)
		{};
		
		static int compareElements (AutomatableController* first, AutomatableController* second)
		{
			return (first->number - second->number);
		}

		int getController() { return number; };
		String getDisplayName() { return String (name + String(" (") + String(number) + String(")")); }; // should use format!
		Colour getColour() { return colour; };
		void setColour(const Colour colour_) { colour = colour_; };

	protected:
		int number;
		String name;
		Colour colour;
	};
	OwnedArray<AutomatableController> controllers;

	int gridLeftX;
	int currentController;
};

//==============================================================================
// Header for the listbox, with add and delete buttons for managing CC lanes.
class AutomationListHeaderComponent  : public Component,
                                  public ButtonListener
{
public:

    AutomationListHeaderComponent (AutomationEditComponent* owner_)
        : owner (owner_)
    {
        addAndMakeVisible (addButton = new TextButton (String::empty));
        addButton->setButtonText (T("add"));
        addButton->setTooltip (T("Add a new controller"));
        addButton->setConnectedEdges (Button::ConnectedOnRight | Button::ConnectedOnLeft);
        addButton->addButtonListener (this);

        addAndMakeVisible (removeButton = new TextButton (String::empty));
        removeButton->setButtonText (T("del"));
        removeButton->setTooltip (T("Delete selected controller"));
        removeButton->setConnectedEdges (Button::ConnectedOnRight | Button::ConnectedOnLeft);
        removeButton->addButtonListener (this);
        
        setSize (100, 18);
    }

    ~AutomationListHeaderComponent ()
    {
        deleteAndZero (addButton);
        deleteAndZero (removeButton);
    }

    void paint (Graphics& g)
    {
        const Colour background (Config::getInstance ()->getColour (T("bookmarksHeader"))); // should have our own colour, or generalise listboxheader colour

        g.setGradientFill (ColourGradient(background, 0.0f, 0.0f,
                                          background.darker (0.1f),
                                          0.0f,
                                          getHeight () - 1.0f,
                                          false));
        g.fillAll();
    }    
    
    void resized ()
    {
        addButton->setBounds (0, 0, 30, getHeight () - 2);
        removeButton->setBounds (30, 0, 30, getHeight () - 2);
    }

    void buttonClicked (Button* buttonThatWasClicked)
    {
		if (buttonThatWasClicked == addButton)
           owner->addCC();
        else if (buttonThatWasClicked == removeButton)
		{
           owner->deleteCurrentCC();
			owner->setCurrentCC(); // ensure something is selected
		}
	}
	
protected:
    AutomationEditComponent* owner;

    TextButton* addButton;
    TextButton* removeButton;
};

//==============================================================================
AutomationEditComponent::AutomationEditComponent(MidiSequencePlugin* _plugin, Transport* _transport)
:
	plugin(_plugin),
	transport(_transport),
    automationGrid (0),
    automationViewport (0),
	gridLeftX (120)
{
    // grid
    automationGrid = new AutomationGrid ();
    automationGrid->setTimeDivision (transport->getTimeDenominator());
    automationGrid->setSnapQuantize (plugin->getIntValue (PROP_SEQNOTESNAP, 4));
    automationGrid->setNumBars (plugin->getIntValue (PROP_SEQBAR, transport->getNumBars()));
    automationGrid->setBarWidth (plugin->getIntValue (PROP_SEQCOLSIZE, 80));
    automationGrid->setListener (plugin);

    addAndMakeVisible (automationViewport = new Viewport (String::empty));
    automationViewport->setScrollBarsShown (true, true);
    automationViewport->setScrollBarThickness (12);
    automationViewport->setViewedComponent (automationGrid);
	
	addAndMakeVisible(leftPane = new ListBox(String("AutomatableControllers"), this));
    leftPane->setMultipleSelectionEnabled (false);
    leftPane->setRowHeight (18);
    leftPane->setOutlineThickness (2);
	leftPane->selectRow(0);

	leftPane->setHeaderComponent(new AutomationListHeaderComponent (this));

	setCurrentCC();
}

AutomationEditComponent::~AutomationEditComponent()
{
	deleteAllChildren ();
}

void AutomationEditComponent::addCC()
{
	int newOne = GetNextControllerNumber();
	controllers.addIfNotAlreadyThere(new AutomatableController(newOne, "CC", GetNextControllerColour()));

	AutomatableController sorter;
	controllers.sort(sorter);
	leftPane->updateContent();

	setCurrentCC(newOne);
}

void AutomationEditComponent::deleteCurrentCC(bool notifyListener)
{
	int curSel = leftPane->getSelectedRow();
	if (curSel >= 0 && curSel < controllers.size())
	{
		AutomatableController* deleteMe = controllers.getUnchecked(curSel);
		automationGrid->removeAllEvents(deleteMe->getController(), notifyListener);
		controllers.remove(curSel);

		AutomatableController sorter;
		controllers.sort(sorter);
		leftPane->updateContent();
	}
}

void AutomationEditComponent::clearCCs(bool notifyListener)
{
	for (int i=controllers.size()-1; i>=0; i--)
	{
		setCurrentCC(controllers.getUnchecked(i)->getController());
		deleteCurrentCC(notifyListener);
	}
}

void AutomationEditComponent::setCurrentCC(int controller)
{
	int laneId = findIndexOfCC(controller);
	if (getNumRows() > 0)
	{
		laneId = laneId > 0 ? laneId : 0;
		leftPane->selectRow(laneId);
		selectedRowsChanged(laneId);
	}
	else 
	{
		// set up a default controller
		addCC();
	}
}

Colour AutomationEditComponent::GetNextControllerColour()
{
	Colour nextColour = Colour(255, 0, 0); // start with red
	int numCCs = controllers.size();
	float rotateAmount = (1.0 / 5.25); // rotate around the hue wheel, 5 colours each rotation, 4 rotations before colour repeats = 30 total uniquely-coloured lanes
	nextColour = nextColour.withRotatedHue(rotateAmount * numCCs);
	return nextColour;
}

int AutomationEditComponent::GetNextControllerNumber()
{
	int controller = 0;
	AutomatableController* last = controllers.getLast();
	if (last != NULL)
		controller = last->getController() + 1;
	return controller;
}

int AutomationEditComponent::findIndexOfCC(int controller)
{
	int laneId = -1;
	for (int i = 0; i < controllers.size(); i++)
		if (controllers[i]->getController() == controller)
			laneId = i;
	return laneId;
}

void AutomationEditComponent::addAutomationEvent(int controller, double value, double beat)
{
	Colour addColour = GetNextControllerColour();
	AutomatableController sorter;
	controllers.sort(sorter);
	int laneId = findIndexOfCC(controller);
	
	if (laneId == -1)
	{
		controllers.addIfNotAlreadyThere(new AutomatableController(controller, "CC", addColour));
		leftPane->updateContent();
	}
	else
		addColour = controllers.getUnchecked(laneId)->getColour();

	automationGrid->addEvent(controller, value, beat, addColour);
}
	
int AutomationEditComponent::getNumRows()
{
    return controllers.size ();
}

void AutomationEditComponent::paintListBoxItem (int rowNumber,
                                           Graphics& g,
                                           int width, int height,
                                           bool rowIsSelected)
{
    AutomatableController* ctrl = controllers.getUnchecked (rowNumber);
    if (rowIsSelected)
	{
        g.setColour (ctrl->getColour().darker(0.5));
		g.drawRect (1, 1, width - 2, height-1);
        g.setColour (ctrl->getColour());
	}
    else
        g.setColour (ctrl->getColour().brighter(0.5));
    g.fillRect (2, 2, width - 4, height - 3);

    g.setColour (Colours::black);
    g.setFont (height * 0.7f);

    g.drawText (ctrl->getDisplayName(),
                2 + 4, 2, width - 4, height - 2,
                Justification::centredLeft, true);
}

void AutomationEditComponent::selectedRowsChanged (int lastRowSelected)
{
	if (lastRowSelected >= 0 && lastRowSelected < controllers.size())
	{
		AutomatableController* ctrl = controllers.getUnchecked (lastRowSelected);
		automationGrid->setCurrentController(ctrl->getController(), ctrl->getColour(), ctrl->getColour().darker(0.5));
	}
}

void AutomationEditComponent::resized ()
{
    int paneWidth = getGridLeftX ();

    automationViewport->setBounds (paneWidth,
                              0,
                              getWidth () - paneWidth,
                              getHeight());

    automationGrid->setSize (300, getHeight());
    automationGrid->updateSize ();
	
	leftPane->setBounds(0, 0, paneWidth, getHeight());
}

//==============================================================================
// This is rapidly becoming a reusable MIDI sequence editor component, complete with surrounding controls.
// Still fairly well dependent on midi plugin but split off from the rest of the plugin editor.
class MidiEditorTabContentComponent 
: 
   public Component,
   public Timer,
   public ChangeListener,
   public ComboBoxListener,
   public SliderListener   
{
public:
   MidiEditorTabContentComponent(MidiSequencePlugin* plugin_, Transport* transport_);
   ~MidiEditorTabContentComponent();

   PianoGrid* getPianoGrid();
   PianoGridKeyboard* getMidiKeyboard();
   ComboBox* getNoteLengthBox();

   void updateParameters();

   void changeListenerCallback(void *objectThatHasChanged);
   void comboBoxChanged(ComboBox* comboBoxThatHasChanged);
   void sliderValueChanged(Slider* sliderThatWasMoved);
   void timerCallback();
   void resized();

private:
   MidiSequencePlugin* plugin;
   Transport* transport;

   Label* quantizeLabel;
   ComboBox* quantizeBox;

   Label* barLabel;
   Slider* barSlider;
   Label* zoomLabel;
   ImageSlider* zoomSlider;

   StretchableLayoutManager myLayout;
   StretchableLayoutResizerBar* layoutResizer;

   NoteEditComponent* noteEditor;
   AutomationEditComponent* automationEditor;   
};

MidiEditorTabContentComponent::MidiEditorTabContentComponent(MidiSequencePlugin* plugin_, Transport* transport_)
:
   plugin(plugin_),
   transport(transport_),
   quantizeLabel(0),
   quantizeBox(0),
   barLabel(0),
   barSlider(0),
   zoomLabel(0),
   zoomSlider(0),
   layoutResizer(0),
   noteEditor(0),
   automationEditor(0)
{
   {
      // bar count slider
      addAndMakeVisible (barLabel = new Label (String::empty, "Bars:"));
      addAndMakeVisible (barSlider = new Slider (String::empty));
      barSlider->setRange (1, 999, 1);
      barSlider->setSliderStyle (Slider::IncDecButtons);
      barSlider->setTextBoxStyle (Slider::TextBoxLeft, false, 80, 20);
      barSlider->setValue (transport->getNumBars(), false);
      barSlider->setTooltip (T("Bar count"));
      barSlider->addListener (this);

      // quantize box
      addAndMakeVisible (quantizeLabel = new Label (String::empty, "Quantise:"));
      addAndMakeVisible (quantizeBox = new ComboBox (String::empty));
      quantizeBox->setEditableText (false);
      quantizeBox->setJustificationType (Justification::centredLeft);
      quantizeBox->addItem (T("off"), 1);
      quantizeBox->addItem (T("1 bar"), 2);
      quantizeBox->addItem (T("1/2 bar"), 3);
      quantizeBox->addItem (T("beat"), 5);
      quantizeBox->addItem (T("8th"), 9);
      quantizeBox->addItem (T("16th"), 17);
      quantizeBox->addItem (T("32th"), 33);
      quantizeBox->addItem (T("64th"), 65);
      quantizeBox->setTooltip (T("Snap"));
      quantizeBox->addListener (this);

      quantizeBox->setSelectedId (plugin->getIntValue (PROP_SEQNOTESNAP, 4) + 1);

      myLayout.setItemLayout (0, -0.2, -0.9, -0.6); // piano/note editor 60% default
      myLayout.setItemLayout (1, 8, 8, 8); // resizer bar always 8 pixels
      myLayout.setItemLayout (2, -0.1, -0.8, -0.4); // controlller automation editor 40% default

      addAndMakeVisible(layoutResizer = new StretchableLayoutResizerBar(&myLayout, 1, false));

      addAndMakeVisible(noteEditor = new NoteEditComponent(plugin, transport));
      noteEditor->getMidiKeyboard()->addChangeListener (this);
      noteEditor->getNoteLengthBox()->addListener (this);

         addAndMakeVisible(automationEditor = new AutomationEditComponent(plugin, transport));

      // zoom slider
      addAndMakeVisible (zoomLabel = new Label (String::empty, "Zoom:"));
      addAndMakeVisible (zoomSlider = new ImageSlider ("Zoom:"));
      zoomSlider->setOrientation (ImageSlider::LinearHorizontal);
      zoomSlider->setRange (10, 1024, 1);
      zoomSlider->setSkewFactor (0.5f);
      zoomSlider->setValue (noteEditor->getPianoGrid()->getBarWidth (), false);
      zoomSlider->setSliderStyle (Slider::LinearHorizontal);
      zoomSlider->setTextBoxStyle (Slider::NoTextBox, false, 80, 20);
      zoomSlider->setTooltip (T("Zoom factor"));
      zoomSlider->addListener (this);
   }
      // set up UI with current plugin state
   updateParameters();

   // config our playhead UI refresh timer
   if (transport->isPlaying ())
      startTimer (1000 / 20); // 20 frames per seconds

   transport->addChangeListener(this);
}

MidiEditorTabContentComponent::~MidiEditorTabContentComponent()
{
   transport->removeChangeListener(this);
}

PianoGrid* MidiEditorTabContentComponent::getPianoGrid()
{
   return noteEditor->getPianoGrid();
}

PianoGridKeyboard* MidiEditorTabContentComponent::getMidiKeyboard()
{
   return noteEditor->getMidiKeyboard();
}

ComboBox* MidiEditorTabContentComponent::getNoteLengthBox()
{
   return noteEditor->getNoteLengthBox();
}

void MidiEditorTabContentComponent::resized()
{
   int headHeight = 32;

   // header-centre
   quantizeLabel->setBounds (200, 2, 70, 16);
   quantizeBox->setBounds (275, 2, 60, 16);

   // header-right
   barLabel->setBounds (getWidth () - 210, 2, 45, 16);
   barSlider->setBounds (getWidth () - 170, 2, 60, 16);
   zoomLabel->setBounds (getWidth () - 115, 2, 45, 16);
   zoomSlider->setBounds (getWidth () - 80, 2, 80, 16);

   // content - 2 panes, top for note editor, bottom for automation editor
   Component* comps[] = { noteEditor, layoutResizer, automationEditor }; 
   myLayout.layOutComponents (comps, 3,
      0, headHeight, getWidth(), getHeight() - headHeight,
      true, true);
}

void MidiEditorTabContentComponent::updateParameters()
{
   MidiSequencePlugin* sequencer = plugin;//getPlugin ();
   if (plugin)
   {
      if (barSlider)
         barSlider->setValue(sequencer->getIntValue(PROP_SEQBAR, 4), true);

      if (zoomSlider)
         zoomSlider->setValue(sequencer->getIntValue (PROP_SEQCOLSIZE, 80), false);
      
      if (noteEditor) 
      {
         PianoGrid* pianoGrid = noteEditor->getPianoGrid();
         if (pianoGrid)
         {
            if (quantizeBox)
               quantizeBox->setSelectedId(sequencer->getIntValue (PROP_SEQNOTESNAP, 4) + 1, true);
            double notelen = sequencer->getIntValue (PROP_SEQNOTELENGTH, 4);   
            if (noteEditor->getNoteLengthBox())
               noteEditor->getNoteLengthBox()->setSelectedId(notelen, true);

            pianoGrid->removeAllNotes (false);
            for (int i = 0; i < sequencer->getNumNoteOn (); i++)
            {
               int note = -1;
               float beat = -1;
               float length = 0.0f;
               sequencer->getNoteOnIndexed (i, note, beat, length);

               if (length > 0.0f)
                  pianoGrid->addNote (note, beat, length);
            }

            pianoGrid->resized ();
         }
       }
       if (automationEditor)
       {
           automationEditor->clearCCs(false); // ensure the listener (seq) doesn't delete the real CC data!
           automationEditor->getAutomationGrid()->removeAllEvents(false);
           for (int i = 0; i < sequencer->getNumControllerEvents (); i++)
           {
               int controller = 1;
               double value = 0.0;
               double beat = -1;
               sequencer->getControllerIndexed (i, controller, value, beat);

               automationEditor->addAutomationEvent(controller, value, beat);
           }
           automationEditor->setCurrentCC();
           automationEditor->resized ();
       }
    }
}

void MidiEditorTabContentComponent::timerCallback()
{
	PianoGrid* pianoGrid = noteEditor->getPianoGrid();
	MidiSequencePlugin* seq = plugin;
	double floatSequencePosition = seq ? seq->getLoopBeatPosition() / seq->getLengthInBeats() : 0;
	
    pianoGrid->setIndicatorPosition (floatSequencePosition);
	automationEditor->getAutomationGrid()->setIndicatorPosition (floatSequencePosition);
}

void MidiEditorTabContentComponent::changeListenerCallback(void *objectThatHasChanged)
{
   PianoGrid* pianoGrid = noteEditor->getPianoGrid();
   if (objectThatHasChanged == transport)
   {
      if (transport->isPlaying ())
         startTimer (1000 / 20); // 20 frames per seconds
      else if (! transport->isPlaying ())
         stopTimer ();

      pianoGrid->setIndicatorPosition (transport->getPositionAbsolute ());
      automationEditor->getAutomationGrid()->setIndicatorPosition (transport->getPositionAbsolute ());
   }

   PianoGridKeyboard* midiKeyboard = noteEditor->getMidiKeyboard();

   if (objectThatHasChanged == midiKeyboard)
   {
      pianoGrid->setRowsOffset (midiKeyboard->getLowestVisibleKey());
      pianoGrid->resized ();

      // update plugin
      plugin->setValue (PROP_SEQROWOFFSET, midiKeyboard->getLowestVisibleKey());
   }
}

void MidiEditorTabContentComponent::sliderValueChanged (Slider* sliderThatWasMoved)
{
    MidiSequencePlugin* seq = plugin;
	PianoGrid* pianoGrid = noteEditor->getPianoGrid();

    if (sliderThatWasMoved == zoomSlider)
    {
        const int newBarSize = roundFloatToInt (zoomSlider->getValue ());
        pianoGrid->setBarWidth (newBarSize);
        automationEditor->getAutomationGrid()->setBarWidth (newBarSize);

        // update plugin
        seq->setValue (PROP_SEQCOLSIZE, newBarSize);
    }
    else if (sliderThatWasMoved == barSlider)
    {
        const int newBarCount = roundFloatToInt (barSlider->getValue ());
        pianoGrid->setNumBars (newBarCount);
		automationEditor->getAutomationGrid()->setNumBars (newBarCount);
        pianoGrid->notifyListenersOfTimeSignatureChange ();

        // update plugin
        seq->setValue (PROP_SEQBAR, newBarCount);

        pianoGrid->resized();
		automationEditor->getAutomationGrid()->resized();
    }
}

void MidiEditorTabContentComponent::comboBoxChanged (ComboBox* comboBoxThatHasChanged)
{
	PianoGrid* pianoGrid = noteEditor->getPianoGrid();

    if (comboBoxThatHasChanged == quantizeBox)
    {
         int snap = quantizeBox->getSelectedId () - 1;
        pianoGrid->setSnapQuantize (snap);
        automationEditor->getAutomationGrid()->setSnapQuantize (snap);
        plugin->setValue(PROP_SEQNOTESNAP, snap);
    }
    else if (comboBoxThatHasChanged == noteEditor->getNoteLengthBox())
    {
        float noteLength = pianoGrid->getTimeDivision () / (float) noteEditor->getNoteLengthBox()->getSelectedId ();

        pianoGrid->setNoteLengthInBeats (noteLength);

        plugin->setValue (PROP_SEQNOTELENGTH, noteEditor->getNoteLengthBox()->getSelectedId ());
    }
}

class MidiSequencerConfigTabContentComponent
: 
   public Component,
   public SliderListener,
   public ClipListListener
{
public:
   MidiSequencerConfigTabContentComponent(MidiSequencePlugin* plugin_, Transport* transport_, SequenceComponent* parentComponent_);
   ~MidiSequencerConfigTabContentComponent();
   
   void updateParameters();

   void sliderValueChanged(Slider* sliderThatWasMoved);
   virtual void clipListChanged(ClipListComponent* ctrlThatHasChanged);
   virtual void currentClipChanged(ClipListComponent* ctrlThatHasChanged);
   virtual void clipFilesDropped(ClipListComponent* ctrlThatHasChanged, const StringArray& files);

   void resized();

private:
   MidiSequencePlugin* plugin;
   Transport* transport;
   SequenceComponent* sequenceUIComponent;
   
   Slider* channelNumSlider;

   ToggleButton* enabledButton;
   ParamSlider* ccEnabledSlider;
   Slider* partPatternNumSlider;
   ClipListComponent* clipList;
   ParamSlider* currentClipSlider;
};

MidiSequencerConfigTabContentComponent::MidiSequencerConfigTabContentComponent(MidiSequencePlugin* plugin_, Transport* transport_, SequenceComponent* parentComponent_)
:
   plugin(plugin_),
   transport(transport_),
   sequenceUIComponent(parentComponent_),
   channelNumSlider(0),
   ccEnabledSlider(0),
   partPatternNumSlider(0),
   clipList(0)
{
   addAndMakeVisible (channelNumSlider = new Slider (String::empty));
   channelNumSlider->setRange (0, 16, 1);
   channelNumSlider->setSliderStyle (Slider::IncDecButtons);
   channelNumSlider->setTextBoxStyle (Slider::TextBoxLeft, false, 80, 20);
   channelNumSlider->setValue (plugin_->getMidiChannel(), false);
   channelNumSlider->setTooltip (T("MIDI Channel"));
   channelNumSlider->addListener (this);

   const int patternsPerPart = MAXPATTERNSPERPART;
   AudioParameter* theEnablyParameter = plugin->getParameterObject(MIDISEQ_PARAMID_SEQENABLED);
   addAndMakeVisible(ccEnabledSlider = new ParamSlider(plugin, theEnablyParameter, MIDISEQ_PARAMID_SEQENABLED));
   ccEnabledSlider->setTextBoxIsEditable(false);

   addAndMakeVisible(partPatternNumSlider = new Slider("Pattern Number"));
   partPatternNumSlider->setSliderStyle(Slider::IncDecButtons);
   partPatternNumSlider->setRange(0, patternsPerPart, 1);
   partPatternNumSlider->addListener(this);
   
   addAndMakeVisible(clipList = new ClipListComponent("Clip list", "*.mid;*.midi"));
   clipList->addListener(this);

   AudioParameter* theCurrentClipParameter = plugin->getParameterObject(MIDISEQ_PARAMID_CURRENTCLIP);
   addAndMakeVisible(currentClipSlider = new ParamSlider(plugin, theCurrentClipParameter, MIDISEQ_PARAMID_CURRENTCLIP));
   currentClipSlider->setTextBoxIsEditable(false);
}

MidiSequencerConfigTabContentComponent::~MidiSequencerConfigTabContentComponent()
{
   clipList->removeListener(this);
   channelNumSlider->removeListener(this);
   partPatternNumSlider->removeListener(this);
   deleteAllChildren();
}

void MidiSequencerConfigTabContentComponent::updateParameters()
{
   if (channelNumSlider)
      channelNumSlider->setValue(plugin->getMidiChannel(), false);
    
   if (partPatternNumSlider)
      partPatternNumSlider->setValue(plugin->getPatternNumberInPart(), false);
   if (ccEnabledSlider)
      ccEnabledSlider->setValue(plugin->getParameterReal(MIDISEQ_PARAMID_SEQENABLED), false);
      
   if (currentClipSlider)
      currentClipSlider->setValue(plugin->getParameterReal(MIDISEQ_PARAMID_CURRENTCLIP), false);
   if (clipList)
   {
      for (int i=0; i<=plugin->getMaxUsedClipIndex(); i++)
         clipList->setClipFile(i, plugin->getClipFile(i));
      clipList->setCurrentClipIndex(plugin->getCurrentClipIndex(), false); // don't notify
   }
}

void MidiSequencerConfigTabContentComponent::resized()
{
   int rowHeight = 32;
   int curY = 2;

   channelNumSlider->setBounds(10, curY, 80, 16);
   curY += rowHeight;

   ccEnabledSlider->setBounds(10, curY, 80, 16);
   partPatternNumSlider->setBounds(100, curY, 75, 16);
   curY += rowHeight;
   
   currentClipSlider->setBounds(10, curY, 80, 16);
   clipList->setBounds (100, curY, 100, 16);
   curY += rowHeight;
}

void MidiSequencerConfigTabContentComponent::sliderValueChanged (Slider* sliderThatWasMoved)
{
   if (sliderThatWasMoved == partPatternNumSlider)
   {
      plugin->setPatternNumberInPart(partPatternNumSlider->getValue());
      ccEnabledSlider->updateText();
   }
   if (sliderThatWasMoved == channelNumSlider)
   {
      plugin->setValue(PROP_SEQMIDICHANNEL, channelNumSlider->getValue());
      if (plugin->getMidiOutputChannel() != -1 && plugin->getMidiOutputChannel() != plugin->getMidiChannel())
         plugin->setMidiOutputChannelFilter(plugin->getMidiChannel());
   }

}

void MidiSequencerConfigTabContentComponent::clipListChanged(ClipListComponent* ctrlThatHasChanged)
{

}

void MidiSequencerConfigTabContentComponent::currentClipChanged(ClipListComponent* ctrlThatHasChanged)
{
   plugin->setCurrentClipIndex(ctrlThatHasChanged->getCurrentClipIndex()); 
   sequenceUIComponent->getEditorTab()->updateParameters(); // get the new notes in the edit
}

void MidiSequencerConfigTabContentComponent::clipFilesDropped(ClipListComponent* ctrlThatHasChanged, const StringArray& files)
{
   plugin->importClipFiles(files);
   updateParameters(); // get the new clips in the combo
   sequenceUIComponent->getEditorTab()->updateParameters(); // get the new notes in the edit
}

//==============================================================================
SequenceComponent::SequenceComponent (MidiSequencePluginBase* plugin_)
  : PluginEditorComponent (plugin_),
   transport(0),
   tabs(0),
   editorTabContent(0),
   configTabContent(0)
{
   MidiSequencePlugin* seq = getPlugin();
   transport = seq->getParentHost()->getTransport();

   seq->addChangeListener (this);
   
   // configure tab bar for the different UI panes
   addAndMakeVisible(tabs = new TabbedComponent(TabbedButtonBar::TabsAtBottom));
   editorTabContent = new MidiEditorTabContentComponent(seq, transport);
   tabs->addTab("Editor", Colours::aliceblue, editorTabContent, true, -1);
   configTabContent = new MidiSequencerConfigTabContentComponent(seq, transport, this);
   tabs->addTab("Options", Colours::bisque, configTabContent, true, -1);
   
   // set up UI with current plugin state
   updateParameters();
}

SequenceComponent::~SequenceComponent ()
{
    deleteAllChildren ();
}

//==============================================================================
void SequenceComponent::resized ()
{
   int headHeight = 32;

   tabs->setBounds(0, headHeight, getWidth(), getHeight() - headHeight);
}

//==============================================================================
void SequenceComponent::mouseWheelMove (const MouseEvent& e,
                                        float incrementX,
                                        float incrementY)
{
#if 0 
returning soon!
	PianoGrid* pianoGrid = editorTabContent->getPianoGrid();
	PianoGridKeyboard* midiKeyboard = editorTabContent->getMidiKeyboard();
	
    int note = pianoGrid->getRowsOffset();

    int scrollStep = 1;

    if (incrementY < 0)
        note = (note - 1) / scrollStep;
    else
        note = note / scrollStep + 1;

    note = jmin (jmax (note * scrollStep, 0), 127 - pianoGrid->getVisibleRows ());

    midiKeyboard->setLowestVisibleKey (note);

    pianoGrid->setRowsOffset (midiKeyboard ? midiKeyboard->getLowestVisibleKey() : note);
    pianoGrid->resized ();

    // update plugin
    getPlugin ()->setValue (PROP_SEQROWOFFSET, midiKeyboard->getLowestVisibleKey());
#endif 
}

//==============================================================================
void SequenceComponent::updateParameters ()
{
   editorTabContent->updateParameters();
   configTabContent->updateParameters();
}

//==============================================================================
void SequenceComponent::changeListenerCallback (void *objectThatHasChanged)
{
    MidiSequencePlugin* seq = getPlugin ();

    if (objectThatHasChanged == seq)
        updateParameters ();
}

void SequenceComponent::buttonClicked (Button* buttonThatWasClicked)
{

}

void SequenceComponent::comboBoxChanged (ComboBox* comboBoxThatHasChanged)
{

}

void SequenceComponent::sliderValueChanged (Slider* sliderThatWasMoved)
{

}

//==============================================================================

bool SequenceComponent::getMenuBarNames(StringArray& names)
{
    names.add(CommandCategories::file);  
    return true; 
}


bool SequenceComponent::getMenuForMenuName(const String& menuName, PopupMenu& menu) 
{ 
    if (menuName.compare(CommandCategories::file) == 0) 
    {
        menu.addItem(1, "Import..."); 
        menu.addItem(2, "Export..."); 
        return true; 
    }
    return false;
}

void SequenceComponent::menuItemSelected(const String& menuName, int menuItemID)
{
    switch (menuItemID)
    {
    case 1:
        importMidiFile();
        break;
    case 2:
        exportMidiFile();
        break;
    }
};

//==============================================================================
bool SequenceComponent::isInterestedInFileDrag (const StringArray& files)
{
    File file = File(files.joinIntoString(String::empty,0,1));
    if (file.hasFileExtension("mid") || 
        file.hasFileExtension("midi") )
        return true;
    else return false;
}

//==============================================================================
void SequenceComponent::filesDropped(const JUCE_NAMESPACE::StringArray &filenames, int mouseX, int mouseY) 
{
    if (isInterestedInFileDrag(filenames)) 
    {
        String filename = filenames.joinIntoString(String::empty,0,1);
        importMidiFile(File(filename));
    }
}

void SequenceComponent::importMidiFile()
{
    FileChooser myChooser (T("Import a midi file..."),
                           File::nonexistent,
                           JOST_MIDI_WILDCARD, JOST_USE_NATIVE_FILE_CHOOSER);

    if (myChooser.browseForFileToOpen())
    {
        File fileToLoad = myChooser.getResult();

        if (fileToLoad.existsAsFile())
            importMidiFile(fileToLoad);
    }
}

void SequenceComponent::exportMidiFile()
{
    FileChooser myChooser (T("Export a midi file..."),
                           File::nonexistent,
                           JOST_MIDI_WILDCARD, JOST_USE_NATIVE_FILE_CHOOSER);

    if (myChooser.browseForFileToSave(true))
    {
        File fileToLoad = myChooser.getResult();
        exportMidiFile(fileToLoad);
    }
}

//==============================================================================
void SequenceComponent::importMidiFile(const File& file)
{
	FileInputStream midiFileIn(file);
	MidiFile droppedMidiFile;
	droppedMidiFile.readFrom(midiFileIn);
	droppedMidiFile.convertTimestampTicksToSeconds();
	if (droppedMidiFile.getNumTracks() > 0)
      importMidiSequence(droppedMidiFile.getTrack(0));
}

void SequenceComponent::exportMidiFile(const File& file)
{
	MidiFile midiFile;
	int ticksRate = 192;
	midiFile.setTicksPerQuarterNote(ticksRate);

	MidiMessageSequence* seq = getPlugin()->getSequence();
	if (seq)
	{
		MidiMessageSequence saveTrack(*seq);
		saveTrack.addEvent (MidiMessage::timeSignatureMetaEvent (4, 4));

		// convert beats timestamps to ticks
		// would be nice to move this logic to MidiFile (e.g. convertTimestampSecondsToTicks)
		// (however nowhere near general enough yet)
		for (int i = 0; i < saveTrack.getNumEvents (); i++)
		{
			MidiMessage* midiMessage = &saveTrack.getEventPointer (i)->message;
			midiMessage->setTimeStamp(midiMessage->getTimeStamp() * ticksRate);
		}

		midiFile.addTrack(saveTrack);

		FileOutputStream midiFileOut(file);
		midiFile.writeTo(midiFileOut);
	}
}

void SequenceComponent::importMidiSequence(const MidiMessageSequence* midiSeq)
{
    if (midiSeq)
    {
		// fudge * 2 event times to workaround bug in convertTimestampTicksToSeconds?
		for (int i = 0; i < midiSeq->getNumEvents (); i++)
		{
			MidiMessage* midiMessage = &midiSeq->getEventPointer (i)->message;
			midiMessage->setTimeStamp(midiMessage->getTimeStamp() * 2.0);
		}

		getPlugin()->setSequence(midiSeq);				
		updateParameters();

#if 0
// coming back soon, hopefully tidier
		// update the sequencer length to the number of beats in the sequence
		int newBarCount = ceil(midiSeq->getEndTime()) / 4; // assuming 4 beats per bar here

		PianoGrid* pianoGrid = editorTabContent->getPianoGrid();
		barSlider->setValue(newBarCount);
		pianoGrid->setNumBars(newBarCount);
		automationEditor->getAutomationGrid()->setNumBars (newBarCount);
		pianoGrid->notifyListenersOfTimeSignatureChange ();
		getPlugin ()->setValue (PROP_SEQBAR, newBarCount);
		pianoGrid->resized();
		automationEditor->getAutomationGrid()->resized();
#endif
	}
   
}

