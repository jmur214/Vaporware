/* deck.h — card decks for truth-or-dare, in three spice tiers.
 *
 * ── Card format ──────────────────────────────────────────────────────────
 * Every card begins with a digit: the cost of skipping it.  Keeping the cost
 * inside the string means a card and its price can never drift apart, which
 * matters at a hundred-odd entries.  The digit is stripped before rendering.
 *
 *   1  brutal      — skipping is obviously fair, so it is nearly free
 *   2  middling    — skipping costs you something
 *   3  tame        — skipping this is just cowardice, and priced accordingly
 *
 * The font is uppercase A-Z and 0-9 only, so cards are written that way;
 * punctuation would render blank and read as holes.  Keep cards under about
 * sixty characters so they wrap to at most five lines.
 *
 * ── What is in here, and what is not ─────────────────────────────────────
 * The skip is what makes the deck able to be this blunt: nobody has to do
 * anything, and the group decides where its own line is rather than the
 * firmware deciding for them.
 *
 * That reasoning covers the person holding the device.  It does not cover
 * anyone else, so the few things left out are the ones a skip cannot protect
 * against: dares aimed at people outside the room who never agreed to play,
 * anything with a real injury attached, and anything permanent once it has
 * happened.  Everything pointed at the volunteer is fair game and gets to be
 * as filthy or as exposing as it likes.
 */
#ifndef DECK_H
#define DECK_H

/* ── MILD ─────────────────────────────────────────────────────────── */
static const char* const truth_mild[] = {
    "3WHAT IS THE LAST THING YOU SEARCHED FOR",
    "3WHAT IS YOUR MOST USELESS TALENT",
    "3WHO HERE WOULD YOU CALL IN A CRISIS",
    "3WHAT IS THE WORST HAIRCUT YOU EVER HAD",
    "3WHAT APP DO YOU WASTE THE MOST TIME ON",
    "3WHAT IS THE PETTIEST HILL YOU WILL DIE ON",
    "3WHAT IS YOUR WORST HABIT",
    "3WHAT IS THE WEIRDEST THING IN YOUR BAG",
    "3WHAT IS YOUR MOST IRRATIONAL FEAR",
    "2WHAT IS THE LAST PHOTO IN YOUR CAMERA ROLL",
    "2WHAT IS THE CRINGIEST THING YOU HAVE POSTED",
    "3WHO HERE WOULD SURVIVE A ZOMBIE MOVIE",
    "3WHAT ARE YOU WEIRDLY COMPETITIVE ABOUT",
    "2WHAT LIE HAVE YOU TOLD TO GET OUT OF PLANS",
    "3WHO HERE IS THE WORST DRIVER",
    "2WHAT IS THE LAST THING THAT MADE YOU CRY",
    "3WHAT IS YOUR MOST EMBARRASSING PHASE",
    "2WHAT DO YOU PRETEND TO LIKE AND ACTUALLY HATE",
};

static const char* const dare_mild[] = {
    "3TALK IN AN ACCENT UNTIL YOUR NEXT TURN",
    "3DO YOUR BEST IMPRESSION OF SOMEONE HERE",
    "3DO 10 PUSHUPS RIGHT NOW",
    "3SPEAK ONLY IN QUESTIONS FOR 2 MINUTES",
    "2SHOW THE LAST 3 PHOTOS IN YOUR CAMERA ROLL",
    "3DANCE WITH NO MUSIC FOR 20 SECONDS",
    "3SAY THE ALPHABET BACKWARDS",
    "2DO A DRAMATIC READING OF YOUR LAST TEXT",
    "3SWAP ONE ITEM OF CLOTHING WITH SOMEONE",
    "3GIVE EVERYONE HERE A NEW NICKNAME",
    "3TALK WITHOUT USING THE LETTER E",
    "3DO YOUR BEST RUNWAY WALK",
    "3COMPLIMENT EVERYONE IN THE ROOM",
    "3HOLD A PLANK UNTIL YOUR NEXT TURN",
    "2LET SOMEONE RENAME A CONTACT IN YOUR PHONE",
    "3ACT OUT YOUR MORNING ROUTINE IN SILENCE",
    "3SING YOUR SENTENCES UNTIL YOUR NEXT TURN",
    "2LET THE GROUP PICK YOUR NEXT PHOTO CAPTION",
};

/* ── SPICY ────────────────────────────────────────────────────────── */
static const char* const truth_spicy[] = {
    "2WHO HERE WOULD YOU SLEEP WITH IF YOU HAD TO",
    "2WHAT IS YOUR BODY COUNT",
    "1WHAT IS THE MOST EMBARRASSING THING YOU HAVE DONE IN BED",
    "2WHEN DID YOU LAST HOOK UP AND WITH WHO",
    "2WHAT IS YOUR TYPE BE BRUTALLY SPECIFIC",
    "2WHO HERE HAVE YOU THOUGHT ABOUT ROMANTICALLY",
    "1WHAT IS THE WORST PLACE YOU HAVE HOOKED UP",
    "2HAVE YOU EVER CHEATED ON ANYONE",
    "2WHAT IS YOUR BIGGEST TURN OFF",
    "2WHAT IS THE PETTIEST REASON YOU DUMPED SOMEONE",
    "2WHAT IS THE WORST TEXT YOU SENT BY MISTAKE",
    "2WHO IS YOUR CELEBRITY FREE PASS",
    "1WHAT IS A FANTASY YOU HAVE NEVER TOLD ANYONE",
    "2WHO HERE WOULD YOU TRUST WITH YOUR PHONE UNLOCKED",
    "2WHAT RUMOUR ABOUT YOU IS ACTUALLY TRUE",
    "2WHAT IS THE WORST DATE YOU HAVE BEEN ON",
    "2WHAT IS YOUR MOST TOXIC TRAIT WHEN DATING",
    "1WHO HERE WOULD YOU KISS RIGHT NOW",
};

static const char* const dare_spicy[] = {
    "2LET SOMEONE READ YOUR LAST 5 TEXTS ALOUD",
    "1SHOW YOUR SEARCH HISTORY TO THE PERSON LEFT",
    "2LET SOMEONE DRAW ON YOUR FACE",
    "2READ YOUR LAST SENT MESSAGE OUT LOUD",
    "1SWAP PHONES WITH SOMEONE FOR 5 MINUTES",
    "2KISS THE PERSON ON YOUR LEFT ON THE CHEEK",
    "2SIT ON SOMEONES LAP UNTIL YOUR NEXT TURN",
    "2RATE EVERYONE HERE OUT OF 10 OUT LOUD",
    "1CONFESS A CRUSH YOU HAVE HAD ON SOMEONE HERE",
    "2TAKE OFF ONE ITEM OF CLOTHING",
    "2DO YOUR BEST FLIRT ON THE PERSON OPPOSITE",
    "1WHISPER SOMETHING FILTHY TO THE PERSON RIGHT",
    "2LET SOMEONE SCROLL YOUR PHOTOS FOR 30 SECONDS",
    "2GIVE SOMEONE HERE A 20 SECOND MASSAGE",
    "2DEMONSTRATE YOUR KISSING TECHNIQUE ON YOUR HAND",
    "1SAY THE LAST THING YOU SEARCHED IN PRIVATE MODE",
    "2LET THE GROUP PICK YOUR PROFILE PICTURE",
    "1LET SOMEONE ASK ONE QUESTION YOU MUST ANSWER",
};

/* ── UNHINGED ─────────────────────────────────────────────────────── */
static const char* const truth_wild[] = {
    "1WHAT IS THE WORST THING YOU HAVE EVER SEARCHED",
    "1WHAT IS YOUR WEIRDEST TURN ON",
    "1WHO HERE HAVE YOU HAD A SEX DREAM ABOUT",
    "1WHAT IS YOUR KINK AND DO NOT SOFTEN IT",
    "1WHAT IS THE MOST UNHINGED THING IN YOUR NOTES APP",
    "1WHAT WOULD RUIN YOU IF IT GOT OUT",
    "1HAVE YOU EVER BEEN WALKED IN ON AND BY WHO",
    "1WHAT HAVE YOU THOUGHT ABOUT SOMEONE IN THIS ROOM",
    "1WHAT IS THE MOST EMBARRASSING NOISE YOU HAVE MADE",
    "1WHO HERE WOULD YOU NEVER SHARE A BED WITH AND WHY",
    "1WHAT IS THE PETTIEST REVENGE YOU HAVE TAKEN",
    "1WHAT IS THE WORST LIE YOU TOLD A PARTNER",
    "1WHAT IS YOUR MOST DELUSIONAL BELIEF ABOUT YOURSELF",
    "1WHO HERE IS MOST LIKELY TO GET ARRESTED",
    "1WHAT IS THE WORST THING YOU HAVE SAID OUT LOUD",
    "1WHAT IS THE MOST EMBARRASSING THING YOU OWN",
    "1WHAT GRUDGE ARE YOU STILL HOLDING AND AGAINST WHO",
    "1WHAT IS THE WORST THING YOU HAVE DONE COMPLETELY SOBER",
};

static const char* const dare_wild[] = {
    "1LET THE GROUP SEND ONE TEXT AS YOU NO EDITS",
    "1SHOW THE WORST PHOTO ON YOUR PHONE",
    "1LET THE GROUP READ YOUR NOTES APP",
    "1LET SOMEONE SCROLL YOUR DMS FOR 30 SECONDS",
    "1REVEAL YOUR SCREEN TIME AND MOST USED APP",
    "1LET SOMEONE PICK A TATTOO IDEA AND DRAW IT ON YOU",
    "1ANSWER AS SOMEONE HERE FOR THE NEXT 5 MINUTES",
    "1CONFESS YOUR WORST OPINION AND THEN DEFEND IT",
    "1SAY THE MEANEST TRUE THING ABOUT YOURSELF",
    "1ROAST YOURSELF FOR 30 SECONDS STRAIGHT",
    "1LET THE GROUP SET YOUR LOCK SCREEN FOR A WEEK",
    "1TELL EVERYONE HERE WHAT YOU REALLY THINK OF THEM",
    "1LET SOMEONE ASK YOU ANYTHING AND ANSWER FULLY",
    "1READ YOUR OLDEST SAVED MESSAGE OUT LOUD",
    "1ACT OUT YOUR MOST EMBARRASSING MEMORY",
    "1LET THE GROUP GO THROUGH YOUR FOLLOWING LIST",
    "1DESCRIBE YOUR LAST HOOKUP IN THREE WORDS",
    "1SHOW THE LAST THING YOU SENT YOUR BEST FRIEND",
};

#define DECK_N 18u

static const char* const* const g_truth[3] = { truth_mild, truth_spicy, truth_wild };
static const char* const* const g_dare[3]  = { dare_mild,  dare_spicy,  dare_wild  };

/* Skip price lives in the first character; the text starts after it. */
static uint8_t card_cost(const char* c) { return (uint8_t)(c[0] - '0'); }
static const char* card_text(const char* c) { return c + 1; }

#endif /* DECK_H */
