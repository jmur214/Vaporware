/* deck.h — card decks for truth-or-dare, in three spice tiers.
 *
 * ── Card format ──────────────────────────────────────────────────────────
 * Every card begins with a digit: the cost of skipping it.  Keeping the cost
 * inside the string means a card and its price can never drift apart, which
 * matters at over a hundred entries.  The digit is stripped before rendering.
 *
 *   1  brutal      — skipping is obviously fair, so it is nearly free
 *   2  middling    — skipping costs you something
 *   3  tame        — skipping this is just cowardice, and priced accordingly
 *
 * Price tracks how brutal a card actually is, NOT which tier it sits in.  A
 * merely embarrassing card in UNHINGED is still a 2, because pricing it at 1
 * would say "this one is fine to duck" about something that plainly is not.
 * Get that wrong and the whole mechanic stops meaning anything.
 *
 * ── Tiers ────────────────────────────────────────────────────────────────
 * MILD is not an icebreaker.  A card nobody wants to answer is a dead turn,
 * and a deck full of them makes the game limp regardless of tier — so even
 * the bottom tier has teeth, it just keeps away from sex and from anything
 * genuinely private.  SPICY is the dating and sex layer.  UNHINGED is less
 * about sex than about exposure: your phone, your notes, your search bar,
 * the thing you would never say out loud.
 *
 * The font is uppercase A-Z and 0-9 only, so cards are written that way;
 * punctuation renders blank and reads as holes.  Keep cards under about
 * fifty-six characters so they wrap to at most five lines.
 *
 * ── What is left out, and why ────────────────────────────────────────────
 * The skip is what lets this deck be blunt: nobody has to do anything, so the
 * group sets its own line rather than the firmware guessing one for a room it
 * knows nothing about.  That reasoning covers the person holding the device
 * and nobody else, so the exclusions are the cases a skip cannot protect
 * against — dares aimed at people outside the room who never agreed to play,
 * anything with a real injury attached, and anything that cannot be taken
 * back once it has happened.  Aimed at the volunteer, anything goes.
 */
#ifndef DECK_H
#define DECK_H

/* ── MILD — no sex, but never polite ──────────────────────────────── */
static const char* const truth_mild[] = {
    "3WHAT IS THE PETTIEST REASON YOU UNFOLLOWED SOMEONE",
    "2WHO HERE HAVE YOU GOSSIPED ABOUT THE MOST",
    "2WHAT IS THE LAST THING YOU LIED ABOUT",
    "2WHAT RUMOUR HAVE YOU STARTED",
    "3WHAT IS THE DUMBEST WAY YOU HAVE HURT YOURSELF",
    "2WHAT IS THE MOST EMBARRASSING THING IN YOUR SEARCH BAR",
    "3WHAT COMPLIMENT HAVE YOU GIVEN AND NOT MEANT",
    "2WHO HERE WOULD YOU VOTE OUT FIRST AND WHY",
    "2WHAT IS THE WORST TEXT YOU HAVE SENT AT 3AM",
    "3WHAT DO YOU JUDGE PEOPLE FOR INSTANTLY",
    "2WHAT IS THE WORST THING YOU HAVE DONE AT WORK",
    "2WHAT IS THE BIGGEST LIE ON YOUR CV",
    "3WHO HERE IS MOST LIKELY TO GHOST SOMEONE",
    "3WHAT IS YOUR MOST EMBARRASSING CELEBRITY CRUSH",
    "2WHAT IS THE CRINGIEST THING YOU HAVE EVER POSTED",
    "2WHAT IS THE WEIRDEST THING YOU HAVE DONE ALONE",
    "3WHAT IS THE MOST CHILDISH THING YOU STILL DO",
    "2WHO HERE DO YOU TAKE THE LEAST SERIOUSLY",
    "3WHAT IS THE WORST GIFT YOU PRETENDED TO LIKE",
    "2WHAT HAVE YOU DONE THAT YOU NEVER GOT CAUGHT FOR",
};

static const char* const dare_mild[] = {
    "2LET THE GROUP READ YOUR LAST OUTGOING TEXT",
    "3DO AN IMPRESSION OF EVERYONE HERE UNTIL GUESSED",
    "2SHOW YOUR MOST RECENT SCREENSHOT",
    "2LET SOMEONE POST ANY EMOJI TO YOUR STORY",
    "3TALK IN A BABY VOICE UNTIL YOUR NEXT TURN",
    "2READ THE LAST NOTE IN YOUR NOTES APP",
    "3DO 20 SECONDS OF YOUR WORST DANCING",
    "2ACT OUT THE LAST ARGUMENT YOU HAD",
    "2LET SOMEONE CHOOSE YOUR RINGTONE FOREVER",
    "2SWAP SHIRTS WITH THE PERSON ON YOUR LEFT",
    "2LET THE GROUP RENAME YOU IN THEIR PHONES",
    "3SPEAK IN THIRD PERSON UNTIL YOUR NEXT TURN",
    "3DO YOUR BEST FAKE CRY FOR 15 SECONDS",
    "2LET SOMEONE DO YOUR HAIR HOWEVER THEY WANT",
    "2SERENADE THE PERSON ON YOUR RIGHT",
    "3TELL A JOKE AND IF NOBODY LAUGHS TAKE A DARE",
    "2LET SOMEONE DRAW ON YOUR ARM IN PEN",
    "2SEND A VOICE NOTE SINGING TO YOUR GROUP CHAT",
    "3LET THE GROUP GIVE YOU A NEW WALK",
    "2LET THE GROUP PICK YOUR NEXT PROFILE PICTURE",
};

/* ── SPICY — the dating and sex layer ─────────────────────────────── */
static const char* const truth_spicy[] = {
    "2WHO HERE WOULD YOU SLEEP WITH IF YOU HAD TO PICK",
    "2WHAT IS YOUR BODY COUNT NO ROUNDING",
    "1WHAT IS THE MOST EMBARRASSING THING YOU HAVE DONE IN BED",
    "1WHO WAS YOUR WORST HOOKUP AND WHY",
    "2WHAT IS YOUR TYPE BE BRUTALLY SPECIFIC",
    "2WHO HERE HAVE YOU IMAGINED KISSING",
    "1WHAT IS THE WORST PLACE YOU HAVE HOOKED UP",
    "1HAVE YOU EVER CHEATED AND DID THEY FIND OUT",
    "2WHAT IS YOUR BIGGEST TURN OFF IN BED",
    "2WHAT IS THE PETTIEST REASON YOU DUMPED SOMEONE",
    "1WHAT IS THE WORST SEXT YOU HAVE EVER SENT",
    "2WHO IS THE LAST PERSON YOU STALKED ONLINE",
    "1WHAT IS A FANTASY YOU HAVE NEVER SAID OUT LOUD",
    "1HOW MANY PEOPLE HERE HAVE YOU THOUGHT ABOUT",
    "2WHAT IS THE LONGEST DRY SPELL YOU HAVE HAD",
    "2WHO HERE IS THE BEST KISSER BASED ON VIBES ALONE",
    "2WHAT IS YOUR MOST TOXIC TRAIT WHEN DATING",
    "2WHO HERE WOULD YOU ACTUALLY DATE",
    "1WHAT IS THE WORST THING YOU HAVE DONE TO AN EX",
    "2WHAT IS THE WORST DATE YOU HAVE EVER BEEN ON",
};

static const char* const dare_spicy[] = {
    "2LET SOMEONE READ YOUR LAST 5 TEXTS ALOUD",
    "1SHOW YOUR SEARCH HISTORY TO THE PERSON LEFT",
    "2KISS THE PERSON ON YOUR LEFT ON THE CHEEK",
    "2SIT ON SOMEONES LAP UNTIL YOUR NEXT TURN",
    "2TAKE OFF ONE ITEM OF CLOTHING",
    "1WHISPER SOMETHING FILTHY TO THE PERSON ON YOUR RIGHT",
    "1CONFESS A CRUSH YOU HAVE HAD ON SOMEONE HERE",
    "2RATE EVERYONE HERE OUT OF 10 OUT LOUD",
    "2GIVE SOMEONE HERE A 20 SECOND MASSAGE",
    "2DEMONSTRATE YOUR KISSING TECHNIQUE ON YOUR HAND",
    "1SWAP PHONES WITH SOMEONE FOR 5 MINUTES",
    "1SAY THE LAST THING YOU SEARCHED IN PRIVATE MODE",
    "2LET SOMEONE SCROLL YOUR PHOTOS FOR 30 SECONDS",
    "2DO YOUR BEST FLIRT ON THE PERSON OPPOSITE",
    "3ACT OUT YOUR TYPE WITHOUT SAYING ANY WORDS",
    "2SEND THE LAST PHOTO YOU TOOK TO THE GROUP CHAT",
    "2DESCRIBE THE PERSON RIGHT LIKE YOU FANCY THEM",
    "1LET SOMEONE ASK ONE QUESTION YOU MUST ANSWER",
    "1GIVE THE PERSON LEFT A 10 SECOND LAP DANCE",
    "2LET THE GROUP WRITE YOUR NEXT DATING APP OPENER",
};

/* ── UNHINGED — exposure, not just sex ────────────────────────────── */
static const char* const truth_wild[] = {
    "1WHAT IS THE WORST THING YOU HAVE EVER SEARCHED",
    "1WHAT IS YOUR WEIRDEST TURN ON",
    "1WHO HERE HAVE YOU HAD A SEX DREAM ABOUT",
    "1WHAT IS YOUR KINK AND DO NOT SOFTEN IT",
    "1WHAT IS THE MOST UNHINGED THING IN YOUR NOTES APP",
    "1WHAT WOULD END YOUR FRIENDSHIPS IF IT GOT OUT",
    "1HAVE YOU EVER BEEN WALKED IN ON AND BY WHO",
    "1WHAT HAVE YOU THOUGHT ABOUT SOMEONE IN THIS ROOM",
    "1WHAT IS THE WORST LIE YOU HAVE TOLD A PARTNER",
    "1WHO HERE WOULD YOU NEVER SHARE A BED WITH AND WHY",
    "1WHAT IS THE MOST DESPERATE THING YOU HAVE DONE",
    "1WHAT IS THE WORST THING YOU HAVE DONE FULLY SOBER",
    "1WHO HERE HAVE YOU LIED TO THE MOST",
    "1WHAT WOULD YOU NEVER TELL YOUR PARENTS",
    "1WHAT IS THE WORST THING YOU HAVE THOUGHT AND NOT SAID",
    "2WHO HERE DO YOU SECRETLY FIND ANNOYING",
    "2WHAT IS YOUR MOST DELUSIONAL BELIEF ABOUT YOURSELF",
    "2WHAT IS THE MOST EMBARRASSING NOISE YOU HAVE MADE",
    "2WHAT IS THE PETTIEST REVENGE YOU HAVE TAKEN",
    "2WHO HERE WOULD YOU BLOCK FIRST",
};

static const char* const dare_wild[] = {
    "1LET THE GROUP SEND ONE TEXT AS YOU NO EDITS",
    "1SHOW THE WORST PHOTO ON YOUR PHONE",
    "1LET THE GROUP READ YOUR NOTES APP",
    "1LET SOMEONE SCROLL YOUR DMS FOR 30 SECONDS",
    "1LET SOMEONE PICK A TATTOO IDEA AND DRAW IT ON YOU",
    "1TELL EVERYONE HERE WHAT YOU REALLY THINK OF THEM",
    "1LET SOMEONE ASK YOU ANYTHING AND ANSWER FULLY",
    "1READ YOUR LAST 3 SEARCHES OUT LOUD",
    "1DESCRIBE YOUR LAST HOOKUP IN DETAIL",
    "1LET SOMEONE GO THROUGH YOUR SAVED POSTS",
    "1CONFESS THE WORST THING YOU HAVE DONE TO A FRIEND",
    "1LET THE GROUP PICK SOMETHING YOU MUST CONFESS",
    "1SHOW THE LAST THING YOU SENT YOUR BEST FRIEND",
    "1LET THE GROUP CHOOSE ONE CONTACT TO VOICE NOTE",
    "2LET THE GROUP SET YOUR LOCK SCREEN FOR A WEEK",
    "2REVEAL YOUR SCREEN TIME AND MOST USED APP",
    "2ROAST YOURSELF FOR 30 SECONDS STRAIGHT",
    "2ACT OUT YOUR MOST EMBARRASSING MEMORY",
    "2LET SOMEONE READ YOUR OLDEST SAVED MESSAGE",
    "2LET THE GROUP WRITE YOUR NEXT STORY CAPTION",
};

#define DECK_N 20u

static const char* const* const g_truth[3] = { truth_mild, truth_spicy, truth_wild };
static const char* const* const g_dare[3]  = { dare_mild,  dare_spicy,  dare_wild  };

/* Skip price lives in the first character; the text starts after it. */
static uint8_t card_cost(const char* c) { return (uint8_t)(c[0] - '0'); }
static const char* card_text(const char* c) { return c + 1; }

#endif /* DECK_H */
