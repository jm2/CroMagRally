/****************************/
/*   	  INPUT.C	   	    */
/* (c)2000 Pangea Software  */
/* By Brian Greenstone      */
/****************************/


/***************/
/* EXTERNALS   */
/***************/

#include "game.h"


/**********************/
/*     PROTOTYPES     */
/**********************/

static void GetLocalKeyStateForPlayer(short playerNum, short gamepadSlot);


/****************************/
/*    CONSTANTS             */
/****************************/



/**********************/
/*     VARIABLES      */
/**********************/



/************************* INIT INPUT *********************************/

void InitInput(void)
{
}

/**************** READ KEYBOARD *************/

void ReadKeyboard(void)
{


			/* READ REAL KEYBOARD & INPUT SPROCKET KEYMAP */

	DoSDLMaintenance();


#if _DEBUG
	if (GetNewKeyState(SDL_SCANCODE_KP_1))		gPlayerInfo[0].superSuspensionTimer	+= 3;
	if (GetNewKeyState(SDL_SCANCODE_KP_2))		gPlayerInfo[0].stickyTiresTimer		+= 3;
	if (GetNewKeyState(SDL_SCANCODE_KP_3))		gPlayerInfo[0].invisibilityTimer	+= 3;
	if (GetNewKeyState(SDL_SCANCODE_KP_4))		gPlayerInfo[0].nitroTimer			+= 3;
	if (GetNewKeyState(SDL_SCANCODE_KP_5))		gPlayerInfo[0].flamingTimer			+= 3;
	if (GetNewKeyState(SDL_SCANCODE_KP_6))		gPlayerInfo[0].frozenTimer			+= 3;
	if (GetNewKeyState(SDL_SCANCODE_KP_DIVIDE))	gPlayerInfo[0].numTokens++;
#endif
}


#pragma mark -

/******************* INIT CONTROL BITS *********************/
//
// Called when each level is initialized.
//

void InitControlBits(void)
{
int	i;

	for (i = 0; i < MAX_PLAYERS; i++)
	{
		gPlayerInfo[i].controlBits = gPlayerInfo[i].controlBits_New = 0;
	}
}


/********************* GET LOCAL KEY STATE *************************/
//
// Called at the end of the Main Loop, after rendering.  Gets the key state of *this* computer
// for the next frame.
//

void GetLocalKeyState(void)
{
		/* SEE IF JUST 1 PLAYER ON THIS COMPUTER */

	if (gActiveSplitScreenMode == SPLITSCREEN_MODE_NONE)
	{
		// Single local player: always read from gamepad slot 0 (where the local gamepad lives),
		// but store in the correct player slot (gMyNetworkPlayerNum for network, 0 for solo)
		GetLocalKeyStateForPlayer(gMyNetworkPlayerNum, 0);
	}

		/* GET KEY STATES FOR BOTH PLAYERS */

	else
	{
		for (int i = 0; i < gNumRealPlayers; i++)
		{
			// Split-screen: each local player maps directly to their gamepad slot
			GetLocalKeyStateForPlayer(i, i);
		}
	}
}



/*********************** GET LOCAL KEY STATE FOR PLAYER ****************************/
//
// Creates a bitfield of information for the player's key state.
// This bitfield is use throughout the game and over the network for all player controls.
//
// INPUT:  	playerNum 		= player slot to store control bits (can be network player num)
//			gamepadSlot		= which local gamepad slot to read from (0 for single player)
//


static void GetLocalKeyStateForPlayer(short playerNum, short gamepadSlot)
{
uint32_t	mask,old;
short	i;

	old = gPlayerInfo[playerNum].controlBits;						// remember old bits
	gPlayerInfo[playerNum].controlBits = 0;							// initialize new bits
	mask = 0x1;


			/* BUILD CURRENT BITFIELD */

	for (i = 0; i < NUM_CONTROL_BITS; i++)
	{
		if (GetNeedState(i, gamepadSlot))							// see if key is down (read from local gamepad slot)
			gPlayerInfo[playerNum].controlBits |= mask;				// set bit in bitfield (store in network player slot)

		mask <<= 1;													// shift bit to next position
	}


			/* BUILD "NEW" BITFIELD */

	gPlayerInfo[playerNum].controlBits_New = (gPlayerInfo[playerNum].controlBits ^ old) & gPlayerInfo[playerNum].controlBits;


			/* COPY ANALOG STEERING */

	gPlayerInfo[playerNum].analogSteering = GetAnalogSteering(gamepadSlot);
}


/********************** GET CONTROL STATE ***********************/
//
// INPUT: control = one of kControlBit_XXXX
//

Boolean GetControlState(short player, uint32_t control)
{
	if (gPlayerInfo[player].controlBits & (1L << control))
		return(true);
	else
		return(false);
}


/********************** GET CONTROL STATE NEW ***********************/
//
// INPUT: control = one of kControlBit_XXXX
//

Boolean GetControlStateNew(short player, uint32_t control)
{
	if (gPlayerInfo[player].controlBits_New & (1L << control))
		return(true);
	else
		return(false);
}


#pragma mark -

/************************ PUSH KEYS ******************************/

void PushKeys(void)
{
#if 0
int	i;

	for (i = 0; i < 4; i++)
	{
		gKeyMapP[i] = gKeyMap[i];
		gNewKeysP[i] = gNewKeys[i];
		gOldKeysP[i] = gOldKeys[i];
	}
#endif
}

/************************ POP KEYS ******************************/

void PopKeys(void)
{
#if 0
int	i;

	for (i = 0; i < 4; i++)
	{
		gKeyMap[i] = gKeyMapP[i];
		gNewKeys[i] = gNewKeysP[i];
		gOldKeys[i] = gOldKeysP[i];
	}
#endif
}
