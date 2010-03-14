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

#ifndef __JUCETICE_JOSTOPPRESSOREDITOR_HEADER__
#define __JUCETICE_JOSTOPPRESSOREDITOR_HEADER__

#include "OppressorPlugin.h"
#include "../../../ui/plugins/PluginEditorComponent.h"


//==============================================================================
/**
    This is the Component that our filter will use as its UI.

    One or more of these is created by the DemoJuceFilter::createEditor() method,
    and they will be deleted at some later time by the wrapper code.

    To demonstrate the correct way of connecting a filter to its UI, this
    class is a ChangeListener, and our demo filter is a ChangeBroadcaster. The
    editor component registers with the filter when it's created and deregisters
    when it's destroyed. When the filter's parameters are changed, it broadcasts
    a message and this editor responds by updating its display.
*/
class OppressorEditor   : public PluginEditorComponent,
                              public ChangeListener,
                              public SliderListener,
							  public Timer
{
public:
    /** Constructor.

        When created, this will register itself with the filter for changes. It's
        safe to assume that the filter won't be deleted before this object is.
    */
    OppressorEditor (OppressorPlugin* const ownerFilter);

    /** Destructor. */
    ~OppressorEditor();

	//==============================================================================
    int getPreferredWidth ()                        { return 564; }
    int getPreferredHeight ()                       { return 104; }
    bool isResizable ()                             { return false; }
    void updateParameters ();
   
    //==============================================================================
    /** Our demo filter is a ChangeBroadcaster, and will call us back when one of
        its parameters changes.
    */
    void changeListenerCallback (void* source);

    void sliderValueChanged (Slider* caller);

    //==============================================================================
    /** Standard Juce paint callback. */
    void paint (Graphics& g);

    /** Standard Juce resize callback. */
    void resized();

	//==============================================================================
	void timerCallback ();
    void computeMeters ();
	
	// Binary resources:

	static const char*  knob_png;
    static const int    knob_pngSize = 68188;

	static const char*  oppskin_jpg;
    static const int    oppskin_jpgSize = 7640;

	static const char*  vuback_jpg;
    static const int    vuback_jpgSize = 1618;

	
	juce_UseDebuggingNewOperator


private:

	enum 
	{    
		thresh, ratio, level, attack, release, limiter
	};

	const static int numSliders = 6;

	ImageKnob* Sliders[numSliders];
	
    //==============================================================================
   
    MidiKeyboardComponent* midiKeyboard;
    Label* infoLabel;
	
	TooltipWindow tooltipWindow;
	
	Image* Theknob;
	Image* Theknob2;
	Image* Theknob3;
	Image* Theknob4;
	Image* Theknob5;
	Image* Theknob6;

	Image* TheVUback;

	MeterComponent* meter;

	Image* background;

    void updateParametersFromFilter();

    // handy wrapper method to avoid having to cast the filter to a DemoJuceFilter
    // every time we need it..
    OppressorPlugin* getFilter() const throw()       { return (OppressorPlugin*) getAudioProcessor(); }
};


#endif
