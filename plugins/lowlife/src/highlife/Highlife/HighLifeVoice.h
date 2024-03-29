/*-
 * Copyright (c) discoDSP
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 * 4. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *        This product includes software developed by discoDSP
 *        http://www.discodsp.com/ and contributors.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL COPYRIGHT HOLDERS OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// HighLife Voice Header                                                                                                               //
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#ifndef __HIGHLIFE_VOICE_HEADER_H__
#define __HIGHLIFE_VOICE_HEADER_H__

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "../StandardHeader.h"
#include "HighLifeFilter.h"
#include "HighLifeLfo.h"
#include "HighLifeEnvelope.h"

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define NUM_GLIDE_MODES		2
#define NUM_PLAYMODES		3
#define NUM_FILTER_MODES	5
#define WAVE_PAD			256
#define RMP_TIME			4096
#define RMP_COEF			(1.0f/float(RMP_TIME/8))
#define MAX_CUE_POINTS		256
#define MAX_BLOCK_EVENTS    2048
#define MAX_PATH            1024

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct RTPAR
{
	float	value;
	float	sense;
	int		ctrln;
};

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct HIGHLIFE_INPUT_STATE
{
	// midi zone
	int	midi_key;
	int	midi_vel;

	// cc# controller
	int	midi_cc[128];
	
	// bend
	int	midi_bend;
	
	// channel aftertouch
	int	midi_chanaft;

	// poly aftertouch
	int	midi_polyaft;
};

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct HIGHLIFE_ZONE
{
	// label and path
	char	name[MAX_PATH];
	char	path[MAX_PATH];
	
	// sample properties
	int		num_channels;
	int		num_samples;
	int		sample_rate;
	int		sample_reserved;

	// musical properties
	int		mp_num_ticks;
	int		mp_synchro;
	int		mp_gain;
	int		mp_pan;

	// loop
	int		loop_start;
	int		loop_end;
	int		loop_mode;
	int		loop_reserved;
	
	// input (lo and high range)
	HIGHLIFE_INPUT_STATE lo_input_range;
	HIGHLIFE_INPUT_STATE hi_input_range;

	// midi properties
	int		midi_root_key;		// MIDI unity note
	int		midi_fine_tune;		// in cents
	int		midi_coarse_tune;	// in semitones
	int		midi_trigger;		// trigger mode (0: at attack, 1: at release)
	int		midi_keycents;		// keycent note displacement

	// cue points
	int		cue_pos[MAX_CUE_POINTS];
	int		num_cues;

	// expansion
	int		res_group;
	int		res_offby;
	int		res_future0;
	int		res_future1;

	// wavedata
	float**	ppwavedata;

	// params
	float sys_pit_rng;
	float vel_mod;
	float vel_amp;

	// glide
	float glide_auto;
	RTPAR glide;

	// filter
	float flt_type;
	float flt_kbd_trk;
	float flt_vel_trk;
	RTPAR flt_cut_frq;
	RTPAR flt_res_amt;

	// amplifier
	RTPAR amp_env_att;
	RTPAR amp_env_dec;
	RTPAR amp_env_sus;
	RTPAR amp_env_rel;
	RTPAR amp_env_amt;
	
	// mod lfo
	float mod_lfo_syn;
	RTPAR mod_lfo_phs;
	RTPAR mod_lfo_rat;
	RTPAR mod_lfo_amp;
	RTPAR mod_lfo_cut;
	RTPAR mod_lfo_res;
	RTPAR mod_lfo_pit;

	// mod envelope
	RTPAR mod_env_att;
	RTPAR mod_env_dec;
	RTPAR mod_env_sus;
	RTPAR mod_env_rel;
	RTPAR mod_env_cut;
	RTPAR mod_env_pit;

	// fx sends
	RTPAR efx_chr_lev;
	RTPAR efx_del_lev;
	RTPAR efx_rev_lev;
};

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct HIGHLIFE_PROGRAM
{
	// program version
	int version;

	// program name
	char name[32];
	
	// waves
	HIGHLIFE_ZONE* pzones;
	int	num_zones;
	
	// program playmode
	int	  ply_mode;

	// fx daft params
	int   efx_dft;
	float efx_dft_frq;
	
	// fx chorus params
	int   efx_chr;
	float efx_chr_del;
	float efx_chr_fdb;
	float efx_chr_rat;
	float efx_chr_mod;
	
	// fx delay params
	int	  efx_del;
	float efx_del_del;
	float efx_del_fdb;
	int   efx_del_cro;
	int	  efx_del_syn;
	
	// fx reverb params
	int	  efx_rev;
	float efx_rev_roo;
	float efx_rev_wid;
	float efx_rev_dam;
	
	// fx rock da disco params
	int   efx_rck;
};

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
int		get_int_param(float const f_param,int const range);
float	get_morph(RTPAR* p,float const x);
float	get_env_rate(float const ws);

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class CHighLifeVoice
{
public:
	CHighLifeVoice(void);
	~CHighLifeVoice(void);

public:
	void	process(HIGHLIFE_PROGRAM* pprg,float* psamplesl,float* psamplesr,float* pchol,float* pchor,float* pdell,float* pdelr, float* prevl,float* prevr,int const numsamples,float const f_pit_bnd,float const f_mod_whl,float const f_tempo,float const f_sample_rate);
	void	trigger(HIGHLIFE_PROGRAM* pprg,int const zone_index,HIGHLIFE_INPUT_STATE* pis,int const pa_last_note,int const legato,float const f_mod_whl,int const i_playmode);
	void	release(void);
	int		is_working(void);
	int		get_int_mode(void);
	void	set_int_mode(int const im);
	double	get_fixed_wave_offset(HIGHLIFE_ZONE* pz,int const cue_index);

public:
	inline void		val_follow(RTPAR* pp,RTPAR* pt);
	inline float	sinc_interpol(double const phase,float* psamples,double const phase_speed,int const num_taps);

public:
    int counter;
	// parameter ramp accumulators
	float	ramp_not_coe;
	float	ramp_pit_bnd;
	float   ramp_mod_whl;

	float	not_coe_dif;
	int		parameter_ramp;
	int		loop_flag;
	
	// trigger info
	int		trigger_count;
	int		trigger_legato;

	// sample accums
	float	f_old_pit_value;
	float	f_sampling_delta;
	double	d_wave_phase;
	
	// midi status
	int		midi_curr_key;
	int		midi_curr_vel;
	int		midi_last_key;
	int		midi_last_vel;
	
	// zone index
	HIGHLIFE_ZONE		rampzone;
	int					playzone;

	// filters
	CHighLifeFilter		flt_l;
	CHighLifeFilter		flt_r;

	// envelopes and lfo
	CHighLifeEnvelope	mod_env;
	CHighLifeEnvelope	amp_env;
	CHighLifeLfo		mod_lfo;
};
#endif

