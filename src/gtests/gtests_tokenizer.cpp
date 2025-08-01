//
// Copyright (c) 2017-2025, Manticore Software LTD (https://manticoresearch.com)
// Copyright (c) 2001-2016, Andrew Aksyonoff
// Copyright (c) 2008-2016, Sphinx Technologies Inc
// All rights reserved
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License. You should have
// received a copy of the GPL license along with this program; if you
// did not, you can find it at http://www.gnu.org/
//

#include <gtest/gtest.h>

#include "sphinxint.h"
#include "tokenizer/tokenizer.h"
#include "tokenizer/tok_internals.h"
#include "sphinxquery/xqparser.h"

// Miscelaneous tests of tokenizer



enum : DWORD
{
	TOK_EXCEPTIONS = 1, TOK_NO_DASH = 2, TOK_NO_SHORT = 4
};

extern const char * g_sTmpfile;
extern const char * g_sMagickTmpfile;
extern const char * g_sMagic;

class TokenizerGtest : public ::testing::Test
{
protected:
	TokenizerRefPtr_c CreateTestTokenizer ( DWORD uMode )
	{
		StrVec_t dWarnings;
		CSphString sError;
		CSphTokenizerSettings tSettings;
		if ( !( uMode & TOK_NO_SHORT ) )
			tSettings.m_iMinWordLen = 2;

		TokenizerRefPtr_c pTokenizer = Tokenizer::Create ( tSettings, nullptr, nullptr, dWarnings, sError );
		if ( !( uMode & TOK_NO_DASH ) )
		{
			Verify ( pTokenizer->SetCaseFolding ( "-, 0..9, A..Z->a..z, _, a..z, U+80..U+FF", sError ) );
			pTokenizer->AddSpecials ( "!-" );
		} else
		{
			Verify ( pTokenizer->SetCaseFolding ( "0..9, A..Z->a..z, _, a..z, U+80..U+FF", sError ) );
			pTokenizer->AddSpecials ( "!" );
		}
		if ( uMode & TOK_EXCEPTIONS )
		{
			Verify ( pTokenizer->LoadSynonyms ( g_sMagickTmpfile, nullptr, dWarnings, sError ) );
		}

		// tricky little shit!
		// we want to create a query mode tokenizer
		// the official way is to Clone() an indexing mode one, so we do that
		// however, Clone() adds backslash as a special
		// and that must be done *after* SetCaseFolding, otherwise it's not special any more
		return pTokenizer->Clone ( SPH_CLONE_QUERY );
	}

	TokenizerRefPtr_c m_pTokenizer;
	TokenizerRefPtr_c& pTokenizer = m_pTokenizer;
	CSphString sError;
};

TEST_F( TokenizerGtest, exceptions_more )
{
	m_pTokenizer = CreateTestTokenizer ( TOK_EXCEPTIONS | TOK_NO_SHORT );
	ASSERT_TRUE ( m_pTokenizer->SetBlendChars ( "+, U+23", sError ) );

	const char * dTests[] =
		{	// for completeness...
			"AT&T!!!",									"AT&T", "!", "!", "!", nullptr,		// exceptions vs specials
			"U.S.AB U.S.A. U.S.B.U.S.D.U.S.U.S.A.F.",	"US", "ab", "USA", "USB", "USD", "US", "USAF", nullptr,
			"Y.M.C.A.",									"y", "m", "c", "a", nullptr,
			"B&E's",									"b", "e", "s", nullptr,

			// exceptions vs spaces
			"AT & T",									"AT & T", nullptr,
			"AT  &  T",									"AT & T", nullptr,
			"AT      &      T",							"AT & T", nullptr,
			"AT$&$T",									"at", "t", nullptr,

			// prefix fun
			"U.S.A.X.",									"USA", "x", nullptr,
			"U.X.U.S.A.",								"u", "x", "USA", nullptr,

			// exceptions vs blended
			"#test this",								"#test", "test", "this", nullptr,
			"#test       this",							"#test", "test", "this", nullptr,
			"test#that",								"test#that", "test", "that", nullptr,
			"1+2",										"1+2", "1", "2", nullptr,
			"te.st#this",								"te", "st#this", "st", "this", nullptr,
			"U.boat",									"u", "boat", nullptr,

			// regressions
			";foo bar",									";", "foo", "bar", nullptr,

			nullptr
		};

	for ( int iCur = 0; dTests[iCur]; )
	{
		m_pTokenizer->SetBuffer ( ( BYTE * ) dTests[iCur], (int) strlen ( dTests[iCur] ) );
		++iCur;

		for ( BYTE * pToken = m_pTokenizer->GetToken (); pToken; pToken = m_pTokenizer->GetToken () )
		{
			ASSERT_TRUE ( dTests[iCur] );
			ASSERT_STREQ ( ( const char * ) pToken, dTests[iCur] );
			++iCur;
		}

		ASSERT_FALSE ( dTests[iCur] ) << "Failed for " << iCur;
		++iCur;
	}

	TokenizerRefPtr_c pQtok = m_pTokenizer->Clone ( SPH_CLONE_QUERY );

	pQtok->SetBuffer ( ( BYTE * ) "life:)", 6 );
	ASSERT_STREQ ( ( char * ) pQtok->GetToken (), "life:)" );
	ASSERT_FALSE ( pQtok->GetToken () );

	pQtok->SetBuffer ( ( BYTE * ) "life:\\)", 7 );
	ASSERT_STREQ( ( char * ) pQtok->GetToken (), "life:)" );
	ASSERT_FALSE ( pQtok->GetToken () );
}

TEST_F ( TokenizerGtest, special_blended )
{
	pTokenizer = CreateTestTokenizer ( TOK_NO_DASH );
	ASSERT_TRUE ( pTokenizer->SetBlendChars ( "., -", sError ) );
	pTokenizer->AddSpecials ( "-" );
	pTokenizer->AddPlainChars ( "=" );
	ASSERT_TRUE ( pTokenizer->SetBlendMode ( "trim_none, skip_pure", sError ) );

	char sTest10[] = "hello =- =world";
	pTokenizer->SetBuffer ( ( BYTE * ) sTest10, (int) strlen ( sTest10 ) );

	ASSERT_STREQ ( ( const char * ) pTokenizer->GetToken (), "hello" );
	ASSERT_STREQ ( ( const char * ) pTokenizer->GetToken (), "=world" );
}

TEST_F ( TokenizerGtest, noascii_case )
{
	pTokenizer = Tokenizer::Detail::CreateUTF8Tokenizer ();
	ASSERT_TRUE (
		pTokenizer->SetCaseFolding ( "U+410..U+42F->U+430..U+44F, U+430..U+44F, U+401->U+451, U+451", sError ) );
	char sTest20[] = "abc \xD0\xBE\xD0\xBF\xD0\xB0\x58\xD1\x87\xD0\xB0 def";
	pTokenizer->SetBuffer ( ( BYTE * ) sTest20, (int) strlen ( sTest20 ) );
	ASSERT_STREQ ( ( const char * ) pTokenizer->GetToken (), "\xD0\xBE\xD0\xBF\xD0\xB0" );
	ASSERT_STREQ ( ( const char * ) pTokenizer->GetToken (), "\xD1\x87\xD0\xB0" );
	ASSERT_FALSE ( pTokenizer->GetToken () );
}

TEST_F ( TokenizerGtest, utf8_ngrams )
{
	pTokenizer = Tokenizer::Detail::CreateUTF8NgramTokenizer ();
	ASSERT_FALSE ( pTokenizer->SetNgramChars ( "2..4", sError ) );
	ASSERT_TRUE ( pTokenizer->SetCaseFolding ( "0..9, A..Z->a..z, _, a..z", sError ) );
	ASSERT_TRUE ( pTokenizer->SetNgramChars ( "U+410..U+42F->U+430..U+44F, U+430..U+44F", sError ) );
}

TEST_F ( TokenizerGtest, utf8_4bytes_codepoints )
{
	BYTE sTest21[] = "\xF4\x80\x80\x80\x32\x34\x20";
	BYTE sTest22[] = "\xEC\x97\xB0";
	BYTE sRes21[SPH_MAX_WORD_LEN];

	memset ( sRes21, 0, sizeof ( sRes21 ) );
	const BYTE * pTest21 = sTest21;
	int iCode21 = sphUTF8Decode ( pTest21 );
	ASSERT_EQ ( sphUTF8Encode ( sRes21, iCode21 ), 4 );
	ASSERT_EQ ( sTest21[0], sRes21[0] );
	ASSERT_EQ ( sTest21[1], sRes21[1] );
	ASSERT_EQ ( sTest21[2], sRes21[2] );
	ASSERT_EQ ( sTest21[3], sRes21[3] );
	memset ( sRes21, 0, sizeof ( sRes21 ) );
	BYTE * pRes21 = sRes21;
	SPH_UTF8_ENCODE ( pRes21, iCode21 );
	ASSERT_EQ ( sTest21[0], sRes21[0] );
	ASSERT_EQ ( sTest21[1], sRes21[1] );
	ASSERT_EQ ( sTest21[2], sRes21[2] );
	ASSERT_EQ ( sTest21[3], sRes21[3] );

	memset ( sRes21, 0, sizeof ( sRes21 ) );
	pTest21 = sTest22;
	int iCode22 = sphUTF8Decode ( pTest21 );
	ASSERT_EQ ( iCode22, 0xC5F0 );
	ASSERT_EQ ( sphUTF8Encode ( sRes21, iCode22 ), 3 );
	ASSERT_FALSE ( memcmp ( sTest22, sRes21, sizeof ( sTest22 ) ) );
	memset ( sRes21, 0, sizeof ( sRes21 ) );
	pRes21 = sRes21;
	SPH_UTF8_ENCODE ( pRes21, iCode22 );
	ASSERT_FALSE ( memcmp ( sTest22, sRes21, sizeof ( sTest22 ) ) );

	pTokenizer = Tokenizer::Detail::CreateUTF8Tokenizer ();
	pTokenizer->SetBuffer ( ( BYTE * ) sTest21, sizeof ( sTest21 ) );
	ASSERT_STREQ ( ( const char * ) pTokenizer->GetToken (), "\xF4\x80\x80\x80\x32\x34" );
}

TEST_F ( TokenizerGtest, Sentence )
{
	const char * SENTENCE = "\2"; // MUST be in sync with sphinx.cpp
	const char * sTest[] =
	{
		"Bill Gates Jr. attended", "bill", "gates", "jr", "attended", NULL,
		"Very good, Dr. Watson", "very", "good", "dr", "watson", NULL,
		"VERY GOOD, DR. WATSON", "very", "good", "dr", "watson", NULL,
		"He left US. Went abroad", "he", "left", "us", SENTENCE, "went", "abroad", NULL,
		"Known as Mr. Doe", "known", "as", "mr", "doe", NULL,
		"Survived by Mrs. Doe", "survived", "by", "mrs", "doe", NULL,
		"J. R. R. Tolkien", "j", "r", "r", "tolkien", NULL,
		"That is it. A boundary", "that", "is", "it", SENTENCE, "a", "boundary", NULL,
		"Just a sentence. And then some.", "just", "a", "sentence", SENTENCE, "and", "then", "some", SENTENCE, NULL,
		"Right, guy number two? Yes, guy number one!", "right", "guy", "number", "two", SENTENCE, "yes", "guy", "number", "one", SENTENCE, NULL,
		"S.T.A.L.K.E.R. sold well in the U.K and elsewhere. Including Russia.", "s", "t", "a", "l", "k", "e", "r", "sold", "well", "in", "the", "u", "k", "and", "elsewhere", SENTENCE, "including", "russia", SENTENCE, NULL,
		"Yoyodine Inc. exists since 1800", "yoyodine", "inc", "exists", "since", "1800", NULL,
		"John D. Doe, our CEO", "john", "d", "doe", "our", "ceo", NULL,
		"Yoyodine Inc. (the Company)", "yoyodine", "inc", "the", "company", NULL,
		NULL
	};

	CSphTokenizerSettings tSettings;
	tSettings.m_iMinWordLen = 1;

	StrVec_t dWarnings;
	pTokenizer = Tokenizer::Create ( tSettings, nullptr, nullptr, dWarnings, sError );

	ASSERT_TRUE ( pTokenizer->SetCaseFolding ( "-, 0..9, A..Z->a..z, _, a..z, U+80..U+FF", sError ) );
//	ASSERT_TRUE ( pTok->SetBlendChars ( "., &", sError ) ); // NOLINT
	ASSERT_TRUE ( pTokenizer->EnableSentenceIndexing ( sError ) );

	int i = 0;
	while ( sTest[i] )
	{
		pTokenizer->SetBuffer ( ( BYTE * ) sTest[i], (int) strlen ( sTest[i] ) );
		i++;

		BYTE * sTok;
		while ( ( sTok = pTokenizer->GetToken () )!=NULL )
		{
			ASSERT_STREQ ( ( char * ) sTok, sTest[i] );
			i++;
		}

		ASSERT_FALSE ( sTest[i] );
		i++;
	}
}

//////////////////////////////////////////////////////////////////////////

class TokenizerBlended : public TokenizerGtest
{
protected:
	void SetUp () override
	{
		TokenizerGtest::SetUp ();
		m_pTokenizer = CreateTestTokenizer ( 0 );
		ASSERT_TRUE ( m_pTokenizer->SetBlendChars ( "., @", sError ) );
		m_pTokenizer->AddSpecials ( "()!-\"@" );
	}
};

TEST_F ( TokenizerBlended, texas )
{
	char sTest1[] = "(texas.\\\")";
	pTokenizer->SetBuffer ( ( BYTE * ) sTest1, (int) strlen ( sTest1 ) );
	ASSERT_STREQ ( ( const char * ) pTokenizer->GetToken (), "(" );
	ASSERT_STREQ ( ( const char * ) pTokenizer->GetToken (), "texas." );
	ASSERT_TRUE ( pTokenizer->TokenIsBlended () );
	pTokenizer->SkipBlended ();
	ASSERT_STREQ ( ( const char * ) pTokenizer->GetToken (), ")" );
	ASSERT_FALSE ( pTokenizer->GetToken ());
}

TEST_F ( TokenizerBlended, series2003 )
{
	char sTest2[] = "\"series 2003\\-\\\"\"";
	pTokenizer->SetBuffer ( ( BYTE * ) sTest2, (int) strlen ( sTest2 ) );
	ASSERT_STREQ (  ( const char * ) pTokenizer->GetToken (), "\"" );
	ASSERT_STREQ (  ( const char * ) pTokenizer->GetToken (), "series" );
	ASSERT_STREQ (  ( const char * ) pTokenizer->GetToken (), "2003-" );
	ASSERT_STREQ (  ( const char * ) pTokenizer->GetToken (), "\"" );
	ASSERT_FALSE ( pTokenizer->GetToken () );
}

TEST_F ( TokenizerBlended, aa_lock_up_bb )
{
	char sTest3[] = "aa lock.up bb";
	pTokenizer->SetBuffer ( ( BYTE * ) sTest3, (int) strlen ( sTest3 ) );
	ASSERT_STREQ ( ( const char * ) pTokenizer->GetToken (), "aa" );
	ASSERT_FALSE ( pTokenizer->TokenIsBlended () );
	ASSERT_FALSE ( pTokenizer->TokenIsBlendedPart () );
	ASSERT_STREQ ( ( const char * ) pTokenizer->GetToken (), "lock.up" );
	ASSERT_TRUE ( pTokenizer->TokenIsBlended () );
	ASSERT_STREQ (  ( const char * ) pTokenizer->GetToken (), "lock" );
	ASSERT_FALSE ( pTokenizer->TokenIsBlended () );
	ASSERT_TRUE ( pTokenizer->TokenIsBlendedPart () );
	ASSERT_STREQ ( ( const char * ) pTokenizer->GetToken (), "up" );
	ASSERT_FALSE ( pTokenizer->TokenIsBlended () );
	ASSERT_TRUE ( pTokenizer->TokenIsBlendedPart () );
	ASSERT_STREQ ( ( const char * ) pTokenizer->GetToken (), "bb" );
	ASSERT_FALSE ( pTokenizer->TokenIsBlended () );
	ASSERT_FALSE ( pTokenizer->TokenIsBlendedPart () );
}

TEST_F ( TokenizerBlended, text_3rd )
{
	char sTest4[] = "3.rd text";
	pTokenizer->SetBuffer ( ( BYTE * ) sTest4, (int) strlen ( sTest4 ) );
	ASSERT_STREQ (  ( const char * ) pTokenizer->GetToken (), "3.rd" );
	ASSERT_TRUE ( pTokenizer->TokenIsBlended () );
	ASSERT_TRUE ( pTokenizer->SkipBlended ()==1 );
	ASSERT_STREQ ( ( const char * ) pTokenizer->GetToken (), "text" );
	ASSERT_FALSE ( pTokenizer->TokenIsBlended () );
	ASSERT_FALSE ( pTokenizer->GetToken () );
}

TEST_F ( TokenizerBlended, text_123rd )
{
	char sTest5[] = "123\\@rd text";
	pTokenizer->SetBuffer ( ( BYTE * ) sTest5, (int) strlen ( sTest5 ) );
	ASSERT_STREQ (  ( const char * ) pTokenizer->GetToken (), "123@rd" );
	ASSERT_TRUE ( pTokenizer->TokenIsBlended () );
	ASSERT_TRUE ( pTokenizer->SkipBlended ()==2 );
	ASSERT_STREQ (  ( const char * ) pTokenizer->GetToken (), "text" );
	ASSERT_FALSE ( pTokenizer->TokenIsBlended () );
	ASSERT_FALSE ( pTokenizer->GetToken () );
}

TEST_F ( TokenizerBlended, at_ta_c_da_bl_ok_yo_pest )
{
	char sTest6[] = "at.ta\\.c.da\\.bl.ok yo pest";
	pTokenizer->SetBuffer ( ( BYTE * ) sTest6, (int) strlen ( sTest6 ) );
	ASSERT_STREQ (  ( const char * ) pTokenizer->GetToken (), "at.ta.c.da.bl.ok" );
	ASSERT_TRUE ( pTokenizer->TokenIsBlended () );
	ASSERT_TRUE ( pTokenizer->SkipBlended ()==5 );
	ASSERT_STREQ (  ( const char * ) pTokenizer->GetToken (), "yo" );
	ASSERT_STREQ (  ( const char * ) pTokenizer->GetToken (), "pest" );
	ASSERT_FALSE ( pTokenizer->GetToken () );
}

TEST_F ( TokenizerBlended, text_3_at_rd )
{
	char sTest7[] = "3\\@rd text";
	pTokenizer->SetBuffer ( ( BYTE * ) sTest7, (int) strlen ( sTest7 ) );
	ASSERT_STREQ ( ( const char * ) pTokenizer->GetToken (), "3@rd" );
	ASSERT_TRUE ( pTokenizer->TokenIsBlended () );
	ASSERT_TRUE ( pTokenizer->SkipBlended ()==1 ); // because 3 is overshort!
	ASSERT_STREQ (  ( const char * ) pTokenizer->GetToken (), "text" );
	ASSERT_FALSE ( pTokenizer->TokenIsBlended () );
	ASSERT_FALSE ( pTokenizer->GetToken () );
}

//////////////////////////////////////////////////////////////////////////

TEST( UTF8LEN, Test1 )
{
	ASSERT_EQ ( sphUTF8Len ( "ab\0cd", 256 ), 2 );
}

TEST( UTF8LEN, Test2 )
{
	ASSERT_FALSE ( sphUTF8Len ( "", 256 ) );
	ASSERT_FALSE ( sphUTF8Len ( NULL, 256 ) );
}

//////////////////////////////////////////////////////////////////////////

class TokenizerP : public TokenizerGtest, public ::testing::WithParamInterface<DWORD>
{
protected:
	int iRun = 0;
	void SetUp () override
	{
		TokenizerGtest::SetUp ();
		iRun = GetParam();
		m_pTokenizer = CreateTestTokenizer ( ( iRun>=2 ) ? TOK_EXCEPTIONS : 0 );
	}
};

TEST_P( TokenizerP, OneLineTests )
{

	const char * dTests[] =
		{
			"1", "",							NULL,								// test that empty strings work
			"1", "this is my rifle",			"this", "is", "my", "rifle", NULL,	// test that tokenizing works
			"1", "This is MY rifle",			"this", "is", "my", "rifle", NULL,	// test that folding works
			"1", "i-phone",						"i-phone", NULL,					// test that duals (specials in the middle of the word) work ok
			"1", "i phone",						"phone", NULL,						// test that short words are skipped
			"1", "this is m",					"this", "is", NULL,					// test that short words at the end are skipped
			"1", "the -phone",					"the", "-", "phone", NULL,			// test that specials work
			"1", "the!phone",					"the", "!", "phone", NULL,			// test that specials work
			"1", "i!phone",						"!", "phone", NULL,					// test that short words preceding specials are skipped
			"1", "/-hi",						"-", "hi", NULL,					// test that synonym-dual but folded-special chars work ok
			"2", "AT&T",						"AT&T", NULL,						// test that synonyms work
			"2", "AT & T",						"AT & T", NULL,						// test that synonyms with spaces work
			"2", "AT    &  T",					"AT & T", NULL,						// test that synonyms with continuous spaces work
			"2", "-AT&T",						"-", "AT&T", NULL,					// test that synonyms with specials work
			"2", "AT&",							"at", NULL,							// test that synonyms prefixes are not lost on eof
			"2", "AT&tee.yo",					"at", "tee", "yo", NULL,			// test that non-synonyms with partially matching prefixes work
			"2", "standarten fuehrer",			"Standartenfuehrer", NULL,
			"2", "standarten fuhrer",			"Standartenfuehrer", NULL,
			"2", "standarten fuehrerr",			"standarten", "fuehrerr", NULL,
			"2", "standarten fuehrer Stirlitz",	"Standartenfuehrer", "stirlitz", NULL,
			"2", "standarten  fuehrer  Zog",	"Standartenfuehrer", "zog", NULL,
			"2", "stand\\arten  fue\\hrer Zog",	"Standartenfuehrer", "zog", NULL,
			"2", "OS/2 vs OS/360 vs Ms-Dos",	"OS/2", "vs", "os", "360", "vs", "MS-DOS", NULL,
			"2", "AT ",							"at", NULL,							// test that prefix-whitespace-eof combo does not hang
			"2", "AT&T&TT",						"AT&T", "tt", NULL,
			"2", "http://OS/2",					"http", "OS/2", NULL,
			"2", "AT*&*T",						"at", NULL,
			"2", "# OS/2's system install",		"OS/2", "system", "install", NULL,
			"2", "IBM-s/OS/2/Merlin",			"ibm-s", "OS/2", "merlin", NULL,
			"2", "U.S.A",						"US", NULL,
			"2", "AT&T!",						"AT&T", "!", NULL,					// exceptions vs specials
			"2", "AT&T!!!",						"AT&T", "!", "!", "!", NULL,		// exceptions vs specials
			"2", "U.S.A.!",						"USA", "!", NULL,					// exceptions vs specials
			"2", "MS DOSS feat.Deskview.MS DOS",			"ms", "doss", "featuring", "deskview", "MS-DOS", NULL,
			"2", g_sMagic,									"test", NULL,
			"2", "U.S. U.S.A. U.S.A.F.",					"US", "USA", "USAF", NULL,
			"2", "U.S.AB U.S.A. U.S.B.U.S.D.U.S.U.S.A.F.",	"US", "ab", "USA", "USB", "USD", "US", "USAF", NULL,
			"3", "phon\\e",						"phone", NULL,
			"3", "\\thephone",					"thephone", NULL,
			"3", "the\\!phone",					"the", "phone", NULL,
			"3", "\\!phone",					"phone", NULL,
			"3", "\\\\phone",					"phone", NULL,						// the correct behavior if '\' is not in charset
			"3", "pho\\\\ne",					"pho", "ne", NULL,
			"3", "phon\\\\e",					"phon", NULL,
			"3", "trailing\\",					"trailing", NULL,
			NULL
		};

	for ( int iCur = 0; dTests[iCur] && ( dTests[iCur++][0]-'0' )<=iRun; )
	{
		m_pTokenizer->SetBuffer ( ( BYTE * ) dTests[iCur], (int) strlen ( dTests[iCur] ) );
		iCur++;

		for ( BYTE * pToken = m_pTokenizer->GetToken (); pToken; pToken = m_pTokenizer->GetToken () )
		{
			ASSERT_TRUE ( dTests[iCur] );
			ASSERT_STREQ ( ( const char * ) pToken, dTests[iCur] );
			iCur++;
		}

		ASSERT_FALSE ( dTests[iCur] );
		iCur++;
	}
}

TEST_P( TokenizerP, MiscOneLineTests )
{
// test misc one-liners
	const char * dTests2[] = { "\xC2\x80\xC2\x81\xC2\x82", "\xC2\x80\xC2\x81\xC2\x82", NULL, NULL };
	for ( int iCur = 0; dTests2[iCur]; )
	{
		m_pTokenizer->SetBuffer ( ( BYTE * ) dTests2[iCur], (int) strlen ( dTests2[iCur] ) );
		iCur++;

		for ( BYTE * pToken = m_pTokenizer->GetToken (); pToken; pToken = m_pTokenizer->GetToken () )
		{
			ASSERT_TRUE ( dTests2[iCur] );
			ASSERT_STREQ ( ( const char * ) pToken, dTests2[iCur] );
			iCur++;
		}

		ASSERT_FALSE ( dTests2[iCur] );
		iCur++;
	}
}

TEST_P( TokenizerP, utf8error )
{
	const char * sLine3 = "hi\xd0\xffh";
	m_pTokenizer->SetBuffer ( ( BYTE * ) sLine3, 4 );
	ASSERT_STREQ ( ( char * ) m_pTokenizer->GetToken (), "hi" );
}


TEST_P( TokenizerP, uberlong )
{
	const int UBERLONG = 4096;

	char * sLine4 = new char[UBERLONG + 1];
	memset ( sLine4, 'a', UBERLONG );
	sLine4[UBERLONG] = '\0';

	char sTok4[SPH_MAX_WORD_LEN + 1];
	memset ( sTok4, 'a', SPH_MAX_WORD_LEN );
	sTok4[SPH_MAX_WORD_LEN] = '\0';

	m_pTokenizer->SetBuffer ( ( BYTE * ) sLine4, (int) strlen ( sLine4 ) );
	ASSERT_STREQ ( ( char * ) m_pTokenizer->GetToken (), sTok4 );
	ASSERT_FALSE ( m_pTokenizer->GetToken () );
	SafeDeleteArray ( sLine4 );
}

TEST_P( TokenizerP, uberlong_synonim_only )
{
	const int UBERLONG = 4096;
	if ( iRun==2 )
	{
		char * sLine4 = new char[UBERLONG + 1];
		memset ( sLine4, '/', UBERLONG );
		sLine4[UBERLONG] = '\0';

		m_pTokenizer->SetBuffer ( ( BYTE * ) sLine4, (int) strlen ( sLine4 ) );
		ASSERT_FALSE( m_pTokenizer->GetToken () );

		for ( int i = 0; i<UBERLONG - 3; i += 3 )
		{
			sLine4[i + 0] = 'a';
			sLine4[i + 1] = 'a';
			sLine4[i + 2] = '/';
			sLine4[i + 3] = '\0';
		}

		m_pTokenizer->SetBuffer ( ( BYTE * ) sLine4, (int) strlen ( sLine4 ) );
		for ( int i = 0; i<UBERLONG - 3; i += 3 )
			ASSERT_STREQ ( ( char * ) m_pTokenizer->GetToken (), "aa" );
		ASSERT_FALSE ( m_pTokenizer->GetToken () );
		SafeDeleteArray ( sLine4 );
	}
}

TEST_P ( TokenizerP, short_token_handling )
{
	const char * dTestsShort[] =
	{
		"ab*",		"ab*",		NULL,
		"*ab",		"*ab",		NULL,
		"abcdef",	"abcdef",	NULL,
		"ab *ab* abc", "*ab*",	NULL,
		NULL
	};

	TokenizerRefPtr_c pShortTokenizer = m_pTokenizer->Clone ( SPH_CLONE_QUERY );
	pShortTokenizer->AddPlainChars ( "*" );

	CSphTokenizerSettings tSettings = pShortTokenizer->GetSettings ();
	tSettings.m_iMinWordLen = 5;
	pShortTokenizer->Setup ( tSettings );


	for ( int iCur = 0; dTestsShort[iCur]; )
	{
		pShortTokenizer->SetBuffer ( ( BYTE * ) ( dTestsShort[iCur] ), (int) strlen ( ( const char * ) dTestsShort[iCur] ) );
		iCur++;
		for ( BYTE * pToken = pShortTokenizer->GetToken (); pToken; pToken = pShortTokenizer->GetToken () )
		{
			ASSERT_TRUE ( dTestsShort[iCur] );
			ASSERT_STREQ ( ( const char * ) pToken, dTestsShort[iCur] );
			iCur++;
		}

		ASSERT_FALSE ( dTestsShort[iCur] );
		iCur++;
	}
}

TEST_P( TokenizerP, boundaries )
{
	CSphString sError;
	ASSERT_TRUE ( m_pTokenizer->SetBoundary ( "?", sError ) );

	char sLine5[] = "hello world? testing boundaries?";
	m_pTokenizer->SetBuffer ( ( BYTE * ) sLine5, (int) strlen ( sLine5 ) );

	ASSERT_STREQ ( ( const char * ) m_pTokenizer->GetToken (), "hello" );
	ASSERT_FALSE ( m_pTokenizer->GetBoundary () );
	ASSERT_STREQ (  ( const char * ) m_pTokenizer->GetToken (), "world" );
	ASSERT_FALSE ( m_pTokenizer->GetBoundary () );
	ASSERT_STREQ ( ( const char * ) m_pTokenizer->GetToken (), "testing" );
	ASSERT_TRUE ( m_pTokenizer->GetBoundary () );
	ASSERT_STREQ ( ( const char * ) m_pTokenizer->GetToken (), "boundaries" );
	ASSERT_FALSE( m_pTokenizer->GetBoundary () );
}

TEST_P( TokenizerP, specials_vs_tokens_start_end_ptrs )
{
	char sLine6[] = "abc!def";
	m_pTokenizer->SetBuffer ( ( BYTE * ) sLine6, (int) strlen ( sLine6 ) );

	ASSERT_STREQ ( ( const char * ) m_pTokenizer->GetToken (), "abc" );
	ASSERT_EQ( *m_pTokenizer->GetTokenStart (), 'a' );
	ASSERT_EQ ( *m_pTokenizer->GetTokenEnd (), '!' );

	ASSERT_STREQ(  ( const char * ) m_pTokenizer->GetToken (), "!" );
	ASSERT_EQ ( *m_pTokenizer->GetTokenStart (), '!' );
	ASSERT_EQ ( *m_pTokenizer->GetTokenEnd (), 'd' );

	ASSERT_STREQ( ( const char * ) m_pTokenizer->GetToken (), "def" );
	ASSERT_EQ ( *m_pTokenizer->GetTokenStart (), 'd' );
	ASSERT_EQ ( *m_pTokenizer->GetTokenEnd (), '\0' );
}

TEST_P( TokenizerP, embedded_zeroes )
{
	char sLine7[] = "abc\0\0\0defgh";
	m_pTokenizer->SetBuffer ( ( BYTE * ) sLine7, 9 );

	ASSERT_STREQ ( ( const char * ) m_pTokenizer->GetToken (), "abc" );
	ASSERT_STREQ ( ( const char * ) m_pTokenizer->GetToken (), "def" );
	ASSERT_FALSE( m_pTokenizer->GetToken () );
	ASSERT_FALSE( m_pTokenizer->GetToken () );
	ASSERT_FALSE( m_pTokenizer->GetToken () );
}

INSTANTIATE_TEST_SUITE_P ( Run3Times, TokenizerP, ::testing::Values ( 1, 2, 3 ) );

//////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////

struct CmpAtomPos_fn
{
	bool IsLess ( const XQKeyword_t * pA, const XQKeyword_t * pB ) const
	{
		return pA->m_iAtomPos<pB->m_iAtomPos;
	}
};


static void CheckQuerySoftSpace ( const XQNode_t * pNode, const int * pQPos, int iCount )
{
	ASSERT_TRUE ( pNode );

	CSphVector<const XQKeyword_t *> dTerms;
	CSphVector<const XQNode_t *> dChildren;

	dChildren.Add ( pNode );

	ARRAY_FOREACH ( i, dChildren )
	{
		const XQNode_t * pChild = dChildren[i];
		for ( auto * pChildren : pChild->dChildren() )
			dChildren.Add ( pChildren );

		for ( auto & dWord : pChild->dWords() )
			dTerms.Add ( &dWord );
	}

	dTerms.Sort ( CmpAtomPos_fn () );

	ASSERT_EQ ( iCount, dTerms.GetLength () );

	ARRAY_FOREACH ( i, dTerms )
	{
		ASSERT_EQ ( dTerms[i]->m_iAtomPos, pQPos[i] );
	}
	ASSERT_STREQ ( dTerms[0]->m_sWord.cstr (), "me" );
	ASSERT_STREQ ( dTerms.Last ()->m_sWord.cstr (), "off" );
}

struct CKeywordHits
{
	const char * m_szKeyword;
	int m_iHits;
};

class QueryParser : public TokenizerGtest
{
protected:
	void TearDown () override
	{
		TokenizerGtest::TearDown ();
	}

	void SetUp () override
	{
		tSchema.AddField ( "title" );
		tSchema.AddField ( "body" );

		TokenizerRefPtr_c pBase = Tokenizer::Detail::CreateUTF8Tokenizer ();
		CSphTokenizerSettings tTokenizerSetup;
		tTokenizerSetup.m_iMinWordLen = 2;
		tTokenizerSetup.m_sSynonymsFile = g_sTmpfile;
		pBase->Setup ( tTokenizerSetup );

		StrVec_t dWarnings;
		ASSERT_TRUE ( pBase->LoadSynonyms ( g_sTmpfile, NULL, dWarnings, sError ) );
		ASSERT_TRUE ( sError.IsEmpty() );

		pTokenizer = sphCloneAndSetupQueryTokenizer ( pBase, true, false, false );

		CSphDictSettings tDictSettings;
		tDictSettings.m_bWordDict = false;
		pDict = sphCreateDictionaryCRC ( tDictSettings, NULL, pTokenizer, "query", false, 32, nullptr, sError );

		ASSERT_TRUE ( pTokenizer );
		ASSERT_TRUE ( pDict );
	}

	void Transform( const char * szQuery, const char * szReconst, const char * szReconstTransformed, const struct CKeywordHits * pKeywordHits = nullptr) const;
	void TestMany ( const char * szQuery, const char * szReconst );

	DictRefPtr_c pDict;
	CSphSchema tSchema;
	CSphIndexSettings tTmpSettings;
	static constexpr CKeywordHits dFuzzerPseudoHits[] = { { "aaa", 0 }, { "bbb", 0 }, { "ccc", 35 }, { "ddd", 63 }, { "eee", 2445 }, { 0, 0 } };
};

void QueryParser::TestMany (const char* szQuery, const char* szReconst)
{
	XQQuery_t tQuery;
	sphParseExtendedQuery ( tQuery, szQuery, nullptr, pTokenizer, &tSchema, pDict, tTmpSettings, nullptr );
	CSphString sReconst = sphReconstructNode ( tQuery.m_pRoot, &tSchema );
	ASSERT_STREQ ( sReconst.cstr(), szReconst );
}

TEST_F ( QueryParser, test_many_01 ) { TestMany ( "aaa bbb ccc"                                     , "( aaa   bbb   ccc )" ); }
TEST_F ( QueryParser, test_many_02 ) { TestMany ( "aaa|bbb ccc"                                     , "( ( aaa | bbb )   ccc )" ); }
TEST_F ( QueryParser, test_many_03 ) { TestMany ( "aaa bbb|ccc"                                     , "( aaa   ( bbb | ccc ) )" ); }
TEST_F ( QueryParser, test_many_04 ) { TestMany ( "aaa (bbb ccc)|ddd"                               , "( aaa   ( ( bbb   ccc ) | ddd ) )" ); }
TEST_F ( QueryParser, test_many_05 ) { TestMany ( "aaa bbb|(ccc ddd)"                               , "( aaa   ( bbb | ( ccc   ddd ) ) )" ); }
TEST_F ( QueryParser, test_many_06 ) { TestMany ( "aaa bbb|(ccc ddd)|eee|(fff)"                     , "( aaa   ( bbb | ( ccc   ddd ) | eee | fff ) )" ); }
TEST_F ( QueryParser, test_many_07 ) { TestMany ( "aaa bbb|(ccc ddd) eee|(fff)"                     , "( aaa   ( bbb | ( ccc   ddd ) )   ( eee | fff ) )" ); }
TEST_F ( QueryParser, test_many_08 ) { TestMany ( "aaa (ccc ddd)|bbb|eee|(fff)"                     , "( aaa   ( ( ccc   ddd ) | bbb | eee | fff ) )" ); }
TEST_F ( QueryParser, test_many_09 ) { TestMany ( "aaa (ccc ddd)|bbb eee|(fff)"                     , "( aaa   ( ( ccc   ddd ) | bbb )   ( eee | fff ) )" ); }
TEST_F ( QueryParser, test_many_10 ) { TestMany ( "aaa \"bbb ccc\"~5|ddd"                           , "( aaa   ( \"bbb ccc\"~5 | ddd ) )" ); }
TEST_F ( QueryParser, test_many_11 ) { TestMany ( "aaa bbb|\"ccc ddd\"~5"                           , "( aaa   ( bbb | \"ccc ddd\"~5 ) )" ); }
TEST_F ( QueryParser, test_many_12 ) { TestMany ( "aaa ( ( \"bbb ccc\"~3|ddd ) eee | ( fff -ggg ) )", "( aaa   ( ( \"bbb ccc\"~3 | ddd )   ( eee | ( fff AND NOT ggg ) ) ) )" ); }
TEST_F ( QueryParser, test_many_13 ) { TestMany ( "@title aaa @body ccc|(@title ddd eee)|fff ggg"   , "( ( @title: aaa )   ( ( @body: ccc ) | ( ( @title: ddd )   ( @title: eee ) ) | ( @body: fff ) )   ( @body: ggg ) )" ); }
TEST_F ( QueryParser, test_many_14 ) { TestMany ( "@title hello world | @body sample program"       , "( ( @title: hello )   ( ( @title: world ) | ( @body: sample ) )   ( @body: program ) )" ); }
TEST_F ( QueryParser, test_many_15 ) { TestMany ( "@title one two three four"                       , "( ( @title: one )   ( @title: two )   ( @title: three )   ( @title: four ) )" ); }
TEST_F ( QueryParser, test_many_16 ) { TestMany ( "@title one (@body two three) four"               , "( ( @title: one )   ( ( @body: two )   ( @body: three ) )   ( @title: four ) )" ); }
TEST_F ( QueryParser, test_many_17 ) { TestMany ( "windows 7 2000"                                  , "( windows   2000 )" ); }
TEST_F ( QueryParser, test_many_18 ) { TestMany ( "aaa a|bbb"                                       , "( aaa   bbb )" ); }
TEST_F ( QueryParser, test_many_19 ) { TestMany ( "aaa bbb|x y z|ccc"                               , "( aaa   bbb   ccc )" ); }
TEST_F ( QueryParser, test_many_20 ) { TestMany ( "a"                                               , "" ); }
TEST_F ( QueryParser, test_many_21 ) { TestMany ( "hello -world"                                    , "( hello AND NOT world )" ); }
TEST_F ( QueryParser, test_many_22 ) { TestMany ( "-hello world"                                    , "( world AND NOT hello )" ); }
TEST_F ( QueryParser, test_many_23 ) { TestMany ( "\"phrase (query)/3 ~on steroids\""               , "\"phrase query on steroids\"" ); }
TEST_F ( QueryParser, test_many_24 ) { TestMany ( "hello a world"                                   , "( hello   world )" ); }
TEST_F ( QueryParser, test_many_25 ) { TestMany ( "-one"                                            , "" ); }
TEST_F ( QueryParser, test_many_26 ) { TestMany ( "-one -two"                                       , "" ); }
TEST_F ( QueryParser, test_many_27 ) { TestMany ( "\"\""                                            , "" ); }
TEST_F ( QueryParser, test_many_28 ) { TestMany ( "\"()\""                                          , "" ); }
TEST_F ( QueryParser, test_many_29 ) { TestMany ( "\"]\""                                           , "" ); }
TEST_F ( QueryParser, test_many_30 ) { TestMany ( "@title hello @body -world"                       , "( ( @title: hello ) AND NOT ( @body: world ) )" ); }
TEST_F ( QueryParser, test_many_31 ) { TestMany ( "Ms-Dos"                                          , "MS-DOS" ); }
TEST_F ( QueryParser, test_many_32 ) { TestMany ( "(@title ^hello @body !aaa)|( hello @body !bbb)"  , "( ( ( @title: hello ) AND NOT ( @body: aaa ) ) | ( hello AND NOT ( @body: bbb ) ) )" ); }

TEST_F ( QueryParser, NEAR_with_NOT )
{
	XQQuery_t tQuery;
	ASSERT_FALSE ( sphParseExtendedQuery ( tQuery, "me -test NEAR/2 off", nullptr, pTokenizer, &tSchema, pDict, tTmpSettings, nullptr ) );
	ASSERT_FALSE ( tQuery.m_pRoot );
}

TEST_F ( QueryParser, long_token )
{
	// edge case token: 41 codepoint, 129 bytes.
	XQQuery_t tQuery;
	ASSERT_TRUE ( sphParseExtendedQuery ( tQuery,
		"00000000\xf6\xaa\xaa\xaa\xf6\xaa\xaa\xaa"
		"\xf3\xaa\xb5\xaa\x30\xf6\xaa\xaa\xaa\xf6\xaa\xaa\xaa\xf6\xaa\xb5"
		"\xaa\xf6\xaa\xaa\xaa\xf6\xaa\xaa\xaa\xf6\xaa\xb5\xaa\xf6\xaa\xaa"
		"\xaa\xf6\xaa\xaa\xaa\xf6\xaa\xaa\xaa\x30\xf6\xaa\xaa\xaa\xf6\xaa"
		"\xaa\xaa\xf6\xb5\xaa\xaa\xf6\xaa\xaa\xab\xf6\xaa\xaa\xaa\xf6\xaa"
		"\xaa\xaa\x30\x30\xf6\xaa\xaa\xaa\xf6\xaa\xaa\xaa\xf6\xb5\xaa\xaa"
		"\xf6\xaa\xaa\xaa\xf6\xaa\xaa\xaa\xf6\xaa\xb5\xaa\xf6\xaa\xaa\xaa"
		"\xf6\xaa\xaa\xaa\xf6\xaa\xaa\xaa\xf3\xaa\xaa\xaa\x30\xf6\xaa\xaa\xaa"
		, nullptr, pTokenizer, &tSchema, pDict, tTmpSettings, nullptr ) );
	ASSERT_TRUE ( tQuery.m_pRoot );
}

TEST_F ( QueryParser, fuzzer_long_token )
{
	XQQuery_t tQuery;
	ASSERT_FALSE ( sphParseExtendedQuery ( tQuery,
		"aaa NOTNEAR/1 (-bbb)"
		, nullptr, pTokenizer, &tSchema, pDict, tTmpSettings, nullptr ) );
	ASSERT_FALSE ( tQuery.m_pRoot );
}

TEST_F ( QueryParser, soft_whitespace1 )
{
	XQQuery_t tQuery;
	ASSERT_TRUE ( sphParseExtendedQuery ( tQuery, "me [ off", nullptr, pTokenizer, &tSchema, pDict, tTmpSettings, nullptr ) );
	int dQpos[] = { 1, 2 };
	SCOPED_TRACE( "soft_whitespace1" );
	CheckQuerySoftSpace ( tQuery.m_pRoot, dQpos, sizeof ( dQpos ) / sizeof ( dQpos[0] ) );
}

TEST_F ( QueryParser, soft_whitespace2 )
{
	XQQuery_t tQuery;
	ASSERT_TRUE ( sphParseExtendedQuery ( tQuery, "me [ ,, &&,[ off", nullptr, pTokenizer, &tSchema, pDict, tTmpSettings, nullptr ) );
	int dQpos[] = { 1, 2 };
	SCOPED_TRACE( "soft_whitespace2" );
	CheckQuerySoftSpace ( tQuery.m_pRoot, dQpos, sizeof ( dQpos ) / sizeof ( dQpos[0] ) );
}

TEST_F ( QueryParser, soft_whitespace3 )
{
	XQQuery_t tQuery;
	ASSERT_TRUE ( sphParseExtendedQuery ( tQuery, "me \xE7\xA7\x81\xE3\x81\xAF\xE3\x82\xAC off", nullptr, pTokenizer, &tSchema, pDict, tTmpSettings, nullptr ) );
	int dQpos[] = { 1, 3 };
	SCOPED_TRACE( "soft_whitespace3" );
	CheckQuerySoftSpace ( tQuery.m_pRoot, dQpos, sizeof ( dQpos ) / sizeof ( dQpos[0] ) );
}

TEST_F ( QueryParser, soft_whitespace4 )
{
	XQQuery_t tQuery;
	ASSERT_TRUE (
		sphParseExtendedQuery ( tQuery, "me \xE7\xA7\x81\xE3\x81\xAF\xE3\x82\xAC \xE7\xA7\x81\xE3\x81\xAF\xE3\x82\xAC \xE7\xA7\x81\xE3\x81\xAF\xE3\x82\xAC off", nullptr, pTokenizer, &tSchema, pDict, tTmpSettings, nullptr ) );
	int dQpos[] = { 1, 3 };
	SCOPED_TRACE( "soft_whitespace4" );
	CheckQuerySoftSpace ( tQuery.m_pRoot, dQpos, sizeof ( dQpos ) / sizeof ( dQpos[0] ) );
}

class CSphDummyIndex : public CSphIndexStub
{
public:
	SmallStringHash_T<int> m_hHits;

			CSphDummyIndex() : CSphIndexStub ( nullptr, nullptr ) {}

	bool	FillKeywords ( CSphVector <CSphKeywordInfo> & dKeywords ) const override;
};


bool CSphDummyIndex::FillKeywords ( CSphVector <CSphKeywordInfo> & dKeywords ) const
{
	ARRAY_FOREACH ( i, dKeywords )
	{
		int * pDocs = m_hHits ( dKeywords[i].m_sTokenized );
		dKeywords[i].m_iDocs = pDocs ? *pDocs : 0;
	}

	return true;
}

void QueryParser::Transform ( const char * szQuery, const char * szReconst, const char *szReconstTransformed, const CKeywordHits * pKeywordHits ) const
{
	CSphIndexSettings tTmpSettings;
	XQQuery_t tQuery;
	if (!sphParseExtendedQuery ( tQuery, szQuery, nullptr, pTokenizer, &tSchema, pDict, tTmpSettings, nullptr ))
		return;

	CSphString sReconst = sphReconstructNode ( tQuery.m_pRoot, &tSchema );

	CSphDummyIndex tIndex;
	if ( pKeywordHits )
	{
		for ( const CKeywordHits * pHits = pKeywordHits; pHits->m_szKeyword; ++pHits )
			EXPECT_TRUE ( tIndex.m_hHits.Add ( pHits->m_iHits, pHits->m_szKeyword ) );
	}
	
	TransformExtendedQueryArgs_t tTranformArgs { true, tQuery.m_bNeedPhraseTransform, &tIndex };
	EXPECT_TRUE ( sphTransformExtendedQuery ( &tQuery.m_pRoot, tTmpSettings, tQuery.m_sParseError, tTranformArgs ) );

	CSphString sReconstTransformed = sphReconstructNode ( tQuery.m_pRoot, &tSchema );
	EXPECT_STREQ ( sReconst.cstr(), szReconst );
	ASSERT_STREQ ( sReconstTransformed.cstr(), szReconstTransformed );
}

TEST_F ( QueryParser, query_transforms_different_order )
{
	Transform (
		"( aaa bbb !xxx ) | ( bbb aaa !yyy )",
		"( ( ( aaa   bbb ) AND NOT xxx ) | ( ( bbb   aaa ) AND NOT yyy ) )",
		"( ( aaa   bbb ) AND NOT ( xxx   yyy ) )"
	);
}

TEST_F ( QueryParser, query_transforms_different_phrases )
{
	Transform (
		"( \"aaa bbb\" !xxx ) | ( \"aaa bbb\" !yyy )",
		"( ( \"aaa bbb\" AND NOT xxx ) | ( \"aaa bbb\" AND NOT yyy ) )",
		"( \"aaa bbb\" AND NOT ( xxx   yyy ) )"
	);
}

TEST_F ( QueryParser, query_transforms_inverted_phrases )
{
	Transform (
		"( \"aaa bbb\" !xxx ) | ( \"bbb aaa\" !yyy )",
		"( ( \"aaa bbb\" AND NOT xxx ) | ( \"bbb aaa\" AND NOT yyy ) )",
		"( ( \"aaa bbb\" AND NOT xxx ) | ( \"bbb aaa\" AND NOT yyy ) )"
	);
}


TEST_F ( QueryParser, transform_common_not_1 )
{
	Transform (
		"( aaa !ccc ) | ( bbb !ccc )",
		"( ( aaa AND NOT ccc ) | ( bbb AND NOT ccc ) )",
		"( ( aaa | bbb ) AND NOT ccc )"
	);
}


TEST_F ( QueryParser, transform_common_not_2 )
{
	Transform (
		"( aaa bbb !ccc) | ( ddd eee !ccc ) ",
		"( ( ( aaa   bbb ) AND NOT ccc ) | ( ( ddd   eee ) AND NOT ccc ) )",
		"( ( ( aaa   bbb ) | ( ddd   eee ) ) AND NOT ccc )"
	);
}

TEST_F ( QueryParser, transform_common_not_3 )
{
	Transform (
		"( aaa bbb !ccc) | ( ddd eee !ccc ) | fff | ( ggg !jjj )",
		"( ( ( aaa   bbb ) AND NOT ccc ) | ( ( ddd   eee ) AND NOT ccc ) | fff | ( ggg AND NOT jjj ) )",
		"( ( ( ( aaa   bbb ) | ( ddd   eee ) ) AND NOT ccc ) | fff | ( ggg AND NOT jjj ) )"
	);
}

TEST_F ( QueryParser, transform_common_not_4 )
{
	Transform (
		"(aaa !bbb) | (ccc !bbb) | (ccc !eee) | (ddd !eee)",
		"( ( aaa AND NOT bbb ) | ( ccc AND NOT bbb ) | ( ccc AND NOT eee ) | ( ddd AND NOT eee ) )",
		"( ( aaa AND NOT bbb ) | ( ccc AND NOT ( bbb   eee ) ) | ( ddd AND NOT eee ) )"
	);
}

TEST_F ( QueryParser, transform_common_not_5 )
{
	Transform (
		"((( aaa & bbb & ccc ) !eee) | ((kkk | jjj & kkk & (zzz | jjj)) !eee))",
		"( ( ( aaa   bbb   ccc ) AND NOT eee ) | ( ( ( kkk | jjj )   kkk   ( zzz | jjj ) ) AND NOT eee ) )",
		"( ( ( aaa   bbb   ccc ) | ( ( kkk | jjj )   kkk   ( zzz | jjj ) ) ) AND NOT eee )"
	);
}

TEST_F ( QueryParser, transform_common_not_6 )
{
	Transform (
		"(aaa !(aaa !nnn)) | (bbb !(aaa !nnn))",
		"( ( aaa AND NOT ( aaa AND NOT nnn ) ) | ( bbb AND NOT ( aaa AND NOT nnn ) ) )",
		"( ( aaa | bbb ) AND NOT ( aaa AND NOT nnn ) )"
	);
}

// COMMON NOT WITH MIXED PHRASES/PROXIMITY terms
TEST_F ( QueryParser, transform_common_not_with_mixed_phrases_terms_1 )
{
	Transform (
		"(aaa !(\"zzz yyy\")) | (bbb !(\"zzz yyy\"~30)) | (ccc !(\"zzz yyy\"~20))",
		"( ( aaa AND NOT \"zzz yyy\" ) | ( bbb AND NOT \"zzz yyy\"~30 ) | ( ccc AND NOT \"zzz yyy\"~20 ) )",
		"( ( aaa | bbb | ccc ) AND NOT \"zzz yyy\"~30 )",
		NULL
	);
}

constexpr CKeywordHits dPseudoHits0[] = { { "nnn", 10 }, { "aaa", 1 }, { "bbb", 1 }, { 0, 0 } };

// COMMON COMPOUND NOT
TEST_F ( QueryParser, transform_common_compound_not_1 )
{
	Transform (
		"(aaa !(nnn ccc)) | (bbb !(nnn ddd))",
		"( ( aaa AND NOT ( nnn   ccc ) ) | ( bbb AND NOT ( nnn   ddd ) ) )",
		"( ( aaa AND NOT ccc ) | ( bbb AND NOT ddd ) | ( ( aaa | bbb ) AND NOT nnn ) )",
		dPseudoHits0
	);
}

TEST_F ( QueryParser, transform_common_compound_not_2 )
{
	Transform (
		"(aaa !(ccc nnn)) | (bbb !(nnn ddd))",
		"( ( aaa AND NOT ( ccc   nnn ) ) | ( bbb AND NOT ( nnn   ddd ) ) )",
		"( ( aaa AND NOT ccc ) | ( bbb AND NOT ddd ) | ( ( aaa | bbb ) AND NOT nnn ) )",
		dPseudoHits0
	);
}

TEST_F ( QueryParser, transform_common_compound_not_3 )
{
	Transform (
		"(aaa !(ccc nnn)) | (bbb !(nnn ddd)) | (ccc !nnn)",
		"( ( aaa AND NOT ( ccc   nnn ) ) | ( bbb AND NOT ( nnn   ddd ) ) | ( ccc AND NOT nnn ) )",
		"( ( aaa AND NOT ccc ) | ( bbb AND NOT ddd ) | ( ( ccc | aaa | bbb ) AND NOT nnn ) )",
		dPseudoHits0
	);
}

TEST_F ( QueryParser, transform_common_compound_not_4 )
{
	Transform (
		"((aaa bbb) !(nnn ccc)) | ((mmm bbb) !(nnn ddd))",
		"( ( ( aaa   bbb ) AND NOT ( nnn   ccc ) ) | ( ( mmm   bbb ) AND NOT ( nnn   ddd ) ) )",
		"( ( ( aaa   bbb ) AND NOT ccc ) | ( ( mmm   bbb ) AND NOT ddd ) | ( ( ( aaa   bbb ) | ( mmm   bbb ) ) AND NOT nnn ) )",
		//
		dPseudoHits0
	);
}

TEST_F ( QueryParser, transform_common_compound_not_5 )
{
	Transform (
		R"a((-"00 000"|"00 000"(0f))|("00"-f0|"00 000"(0)))a",
		R"a(( ( 0f AND NOT ( "00 000" | "00 000" ) ) | ( 00 AND NOT ( f0 | "00 000" ) ) ))a",
		R"a(( ( 0f AND NOT "00 000" ) | ( 00 AND NOT ( f0 | "00 000" ) ) ))a",
		dPseudoHits0
	);
}


TEST_F ( QueryParser, transform_common_compound_not_6 )
{
	Transform (
		"((0)(@*00 -000000)|(@*-000000((0)(-(00 -000)|(00 -000000)|(00 -000)(*))|(-(00 -000)|(00 -000000)|(00 -000)00((0))))))",
		"( ( 00 AND NOT 000000 ) | ( ( ( * AND NOT ( ( 00 AND NOT 000 ) | ( 00 AND NOT 000000 ) | ( 00 AND NOT 000 ) ) ) | ( 00 AND NOT ( ( 00 AND NOT 000 ) | ( 00 AND NOT 000000 ) | ( 00 AND NOT 000 ) ) ) ) AND NOT 000000 ) )",
		"( ( 00 | ( ( * | 00 ) AND NOT ( 00 AND NOT ( 000   000000   000 ) ) ) ) AND NOT 000000 )",
		dPseudoHits0
	);
}


// COMMON COMPOUND NOT WITH MIXED PHRASES/PROXIMITY terms
TEST_F ( QueryParser, transform_common_compound_not_with_mixed_phrases )
{	Transform (
		"(aaa !(ccc \"nnn zzz\"~20)) | (bbb !(\"nnn zzz\"~10 ddd)) | (ccc !\"nnn zzz\")",
		"( ( aaa AND NOT ( ccc   \"nnn zzz\"~20 ) ) | ( bbb AND NOT ( \"nnn zzz\"~10   ddd ) ) | ( ccc AND NOT \"nnn zzz\" ) )",
		"( ( aaa AND NOT ccc ) | ( bbb AND NOT ddd ) | ( ( ccc | aaa | bbb ) AND NOT \"nnn zzz\"~20 ) )",
		dPseudoHits0
	);
}

// COMMON SUBTERM
TEST_F ( QueryParser, transform_common_subterm_1 )
{
	Transform (
		"(aaa (nnn | ccc)) | (bbb (nnn | ddd))",
		"( ( aaa   ( nnn | ccc ) ) | ( bbb   ( nnn | ddd ) ) )",
		"( ( aaa   ccc ) | ( bbb   ddd ) | ( ( aaa | bbb )   nnn ) )",
		dPseudoHits0
	);
}

TEST_F ( QueryParser, transform_common_subterm_2 )
{
	Transform (
		"(aaa (ccc | nnn)) | (bbb (nnn | ddd)) | (ccc | nnn)",
		"( ( aaa   ( ccc | nnn ) ) | ( bbb   ( nnn | ddd ) ) | ( ccc | nnn ) )",
		"( ( aaa   ccc ) | ( bbb   ddd ) | ccc | nnn | ( ( aaa | bbb )   nnn ) )",
		dPseudoHits0
	);
}

constexpr CKeywordHits dPseudoHits1[] = { { "nnn", 10 }, { "aaa", 100 }, { "bbb", 200 }, { 0, 0 } };
TEST_F ( QueryParser, transform_common_subterm_3 )
{
	Transform (
		"(aaa (ccc | nnn)) | (bbb (nnn | ddd))",
		"( ( aaa   ( ccc | nnn ) ) | ( bbb   ( nnn | ddd ) ) )",
		"( ( aaa   ( ccc | nnn ) ) | ( bbb   ( nnn | ddd ) ) )",
		dPseudoHits1
	);
}

TEST_F ( QueryParser, transform_common_3356 )
{
	constexpr CKeywordHits dPseudoHits[] = { {"aaa",0}, {"bbb",0}, {"ccc",0}, {"ddd",0}, {"eee",0}, {"fff",0}, {"ggg",35}, {"hhh",63}, {"iii",2445}, {0,0} };
	Transform ( // obfuscated test case based on #3356. M.b. need XQDEBUG 1
		"(aaa (( ccc (hhh | iii)) | (ddd (fff | iii)) | (eee (ggg | iii)))) | (bbb ((ccc (hhh | iii)) | (ddd (fff | iii)) | (eee (ggg | iii))))",
		"( ( aaa   ( ( ccc   ( hhh | iii ) ) | ( ddd   ( fff | iii ) ) | ( eee   ( ggg | iii ) ) ) ) | ( bbb   ( ( ccc   ( hhh | iii ) ) | ( ddd   ( fff | iii ) ) | ( eee   ( ggg | iii ) ) ) ) )",
		"( ( aaa   eee   iii ) | ( bbb   eee   iii ) | ( ( aaa | bbb )   ccc   hhh ) | ( ( aaa | bbb )   ddd   fff ) | ( ( ( aaa   eee ) | ( bbb   eee ) )   ggg ) | ( ( ( ( aaa | bbb )   ccc ) | ( ( aaa | bbb )   ddd ) )   iii ) )",
		dPseudoHits
	);
}

// COMMON SUBTERM WITH MIXED PHRASES/PROXIMITY terms
TEST_F ( QueryParser, transform_common_subterm_with_mixed_phrases )
{
	constexpr CKeywordHits dPseudoHits[] = { { "nnn", 10 }, { "aaa", 1 }, { "bbb", 2 }, { "qqq", 500 }, { "www", 100 }, { 0, 0 } };
	Transform (
		"(aaa (ccc | \"qqq www\"~10)) | (bbb (\"qqq www\" | ddd)) | (ccc | \"qqq www\"~20)",
		"( ( aaa   ( ccc | \"qqq www\"~10 ) ) | ( bbb   ( \"qqq www\" | ddd ) ) | ( ccc | \"qqq www\"~20 ) )",
		"( ( aaa   ccc ) | ( bbb   ddd ) | ccc | \"qqq www\"~20 | ( ( aaa | bbb )   \"qqq www\"~10 ) )",
		dPseudoHits
	);
}

// COMMON KEYWORDS
TEST_F ( QueryParser, transform_common_keywords_1 )
{
	Transform (
		"\"aaa bbb ccc ddd jjj\" | \"aaa bbb\"",
		"( \"aaa bbb ccc ddd jjj\" | \"aaa bbb\" )",
		"\"aaa bbb\""
	);
}

TEST_F ( QueryParser, transform_common_keywords_2 )
{
	Transform (
		"bbb | \"aaa bbb ccc ddd eee fff ggg\" | bbb  | \"aaa bbb ccc ddd eee fff ggg hhh iii jjj\" |\"hhh iii jjj\"| \"aaa bbb ccc ddd eee fff ggg hhh iii jjj\"| bbb |\"aaa bbb ccc ddd eee fff ggg hhh iii jjj\"| \"aaa bbb ccc ddd eee fff ggg\" | bbb",
		"( bbb | \"aaa bbb ccc ddd eee fff ggg\" | bbb | \"aaa bbb ccc ddd eee fff ggg hhh iii jjj\" | \"hhh iii jjj\" | \"aaa bbb ccc ddd eee fff ggg hhh iii jjj\" | bbb | \"aaa bbb ccc ddd eee fff ggg hhh iii jjj\" | \"aaa bbb ccc ddd eee fff ggg\" | bbb )",
		"( bbb | bbb | \"hhh iii jjj\" | bbb | bbb )"
	);
}

TEST_F ( QueryParser, transform_common_keywords_3 )
{
	Transform (
		"\"aaa bbb ccc ddd jjj\" | \"bbb ccc\"",
		"( \"aaa bbb ccc ddd jjj\" | \"bbb ccc\" )",
		"\"bbb ccc\""
	);
}

TEST_F ( QueryParser, transform_common_keywords_4 )
{
	Transform (
		"\"aaa bbb ccc ddd jjj\" | \"bbb jjj\"",
		"( \"aaa bbb ccc ddd jjj\" | \"bbb jjj\" )",
		"( \"aaa bbb ccc ddd jjj\" | \"bbb jjj\" )"
	);
}

TEST_F ( QueryParser, relaxed_missed_field_with_pos )
{
	Transform (
		"@@relaxed aaa ( bbb @missed[1] ccc )",
		"( aaa   bbb )",
		"( aaa   bbb )"
	);
}

TEST_F ( QueryParser, different_zones )
{
	Transform (
		"(( ZONE:(h1) ACCEL !aaa)|( ACCEL !bbb))",
		"( ( accel AND NOT aaa ) | ( accel AND NOT bbb ) )",
		"( ( accel AND NOT aaa ) | ( accel AND NOT bbb ) )" // not "( accel AND NOT ( aaa   bbb ) )"
	);
}

TEST_F ( QueryParser, different_zonespan )
{
	Transform (
		"(( ZONESPAN:(h1) ACCEL !aaa)|( ACCEL !bbb))",
		"( ( accel AND NOT aaa ) | ( accel AND NOT bbb ) )",
		"( ( accel AND NOT aaa ) | ( accel AND NOT bbb ) )" // not "( accel AND NOT ( aaa   bbb ) )"
	);
}

TEST_F ( QueryParser, memleak_in_transform_phrase )
{
	Transform (
		"\"(aaa|bbb)ccc\"",
		"( \"( aaa | bbb ) ccc\" )",
		"( \"aaa ccc\" | \"bbb ccc\" )"
	);
}

TEST_F ( QueryParser, lost_tail_tokens_from_brackets )
{
	Transform (
		"\"aaa(bbb ccc ddd eee)\"",
		"\"aaa bbb ccc ddd eee\"",
		"\"aaa bbb ccc ddd eee\""
	);
}


TEST_F ( QueryParser, transform_common_keywords_5 )
{
	Transform (
		// FIXME!!! add exact phrase elimination
		"\"aaa bbb ccc\"~10 | \"aaa bbb ccc ddd\"~20 | \"aaa bbb ccc\"~10 | \"aaa bbb ccc\"~10",
		"( \"aaa bbb ccc\"~10 | \"aaa bbb ccc ddd\"~20 | \"aaa bbb ccc\"~10 | \"aaa bbb ccc\"~10 )",
		// "( \"aaa bbb ccc ddd\"~20 | \"aaa bbb ccc\"~10 )",
		"( \"aaa bbb ccc\"~10 | \"aaa bbb ccc ddd\"~20 | \"aaa bbb ccc\"~10 | \"aaa bbb ccc\"~10 )"
	);
}

TEST_F ( QueryParser, transform_common_keywords_6 )
{
	Transform (
		"\"aaa bbb ccc\"~10 | \"aaa bbb ccc ddd\"~10",
		"( \"aaa bbb ccc\"~10 | \"aaa bbb ccc ddd\"~10 )",
		"\"aaa bbb ccc\"~10"
	);
}

TEST_F ( QueryParser, transform_common_keywords_7 )
{
	Transform (
		"\"aaa bbb ccc\"~10 | \"aaa bbb ccc\"~10",
		"( \"aaa bbb ccc\"~10 | \"aaa bbb ccc\"~10 )",
		// "\"aaa bbb ccc\"~10",
		"( \"aaa bbb ccc\"~10 | \"aaa bbb ccc\"~10 )"
	);
}

TEST_F ( QueryParser, transform_common_keywords_8 )
{
	Transform (
		"\"aaa bbb ccc\"~10 | \"aaa bbb ccc\"~9",
		"( \"aaa bbb ccc\"~10 | \"aaa bbb ccc\"~9 )",
		// "\"aaa bbb ccc\"~10",
		"( \"aaa bbb ccc\"~10 | \"aaa bbb ccc\"~9 )"
	);
}

TEST_F ( QueryParser, transform_common_keywords_9 )
{
	Transform (
		"\"aaa bbb ccc ddd eee\" | \"bbb ccc ddd\"~10",
		"( \"aaa bbb ccc ddd eee\" | \"bbb ccc ddd\"~10 )",
		"\"bbb ccc ddd\"~10"
	);
}

TEST_F ( QueryParser, transform_common_keywords_10 )
{
	Transform (
		"\"bbb ccc ddd\"~10 | \"ccc ddd\" | \"aaa bbb\"",
		"( \"bbb ccc ddd\"~10 | \"ccc ddd\" | \"aaa bbb\" )",
		"( \"bbb ccc ddd\"~10 | \"ccc ddd\" | \"aaa bbb\" )"
	);
}

TEST_F ( QueryParser, transform_common_keywords_11 )
{
	Transform (
		"\"aaa bbb ccc ddd eee\" | \"bbb ccc ddd\"~10 | \"ccc ddd\" | \"aaa bbb\"",
		"( \"aaa bbb ccc ddd eee\" | \"bbb ccc ddd\"~10 | \"ccc ddd\" | \"aaa bbb\" )",
		"( \"bbb ccc ddd\"~10 | \"ccc ddd\" | \"aaa bbb\" )"
	);
}

TEST_F ( QueryParser, transform_common_keywords_12 )
{
	Transform (
		"aaa | \"aaa bbb\"~10 | \"aaa ccc\"",
		"( aaa | \"aaa bbb\"~10 | \"aaa ccc\" )",
		"aaa"
	);
}

// COMMON PHRASES
TEST_F ( QueryParser, transform_common_phrases_1 )
{
	Transform (
		"\"aaa bbb ccc ddd\" | \"eee fff ccc ddd\"",
		"( \"aaa bbb ccc ddd\" | \"eee fff ccc ddd\" )",
		"( \"( \"aaa bbb\" | \"eee fff\" ) \"ccc ddd\"\" )"
	);
}

TEST_F ( QueryParser, transform_common_phrases_2 )
{
	Transform (
		"\"ccc ddd aaa bbb\" | \"ccc ddd eee fff\"",
		"( \"ccc ddd aaa bbb\" | \"ccc ddd eee fff\" )",
		"( \"\"ccc ddd\" ( \"aaa bbb\" | \"eee fff\" )\" )"
	);
}

TEST_F ( QueryParser, transform_common_phrases_3 )
{
	Transform (
		"\"aaa bbb ccc ddd\" | \"eee fff ccc ddd\" | \"jjj lll\"",
		"( \"aaa bbb ccc ddd\" | \"eee fff ccc ddd\" | \"jjj lll\" )",
		"( \"jjj lll\" | ( \"( \"aaa bbb\" | \"eee fff\" ) \"ccc ddd\"\" ) )"
	);
}

TEST_F ( QueryParser, transform_common_phrases_4 )
{
	Transform (
		"\"ccc ddd aaa bbb\" | \"ccc ddd eee fff\" | \"jjj lll\"",
		"( \"ccc ddd aaa bbb\" | \"ccc ddd eee fff\" | \"jjj lll\" )",
		"( \"jjj lll\" | ( \"\"ccc ddd\" ( \"aaa bbb\" | \"eee fff\" )\" ) )",
		NULL
	);
}

TEST_F ( QueryParser, transform_common_phrases_5 )
{
	Transform (
		"\"aaa bbb ccc ddd xxx yyy zzz\" | \"eee fff ddd xxx yyy zzz\" | \"jjj lll\"",
		"( \"aaa bbb ccc ddd xxx yyy zzz\" | \"eee fff ddd xxx yyy zzz\" | \"jjj lll\" )",
		"( \"jjj lll\" | ( \"( \"aaa bbb ccc\" | \"eee fff\" ) \"ddd xxx yyy zzz\"\" ) )"
	);
}

TEST_F ( QueryParser, transform_common_phrases_6 )
{
	Transform (
		"\"ddd xxx yyy zzz aaa bbb\" | \"ddd xxx yyy zzz ccc eee fff\" | \"jjj lll\"",
		"( \"ddd xxx yyy zzz aaa bbb\" | \"ddd xxx yyy zzz ccc eee fff\" | \"jjj lll\" )",
		"( \"jjj lll\" | ( \"\"ddd xxx yyy zzz\" ( \"aaa bbb\" | \"ccc eee fff\" )\" ) )"
	);
}

TEST_F ( QueryParser, transform_common_phrases_7 )
{
	Transform (
		"\"xxx zzz ccc ddd\" | \"xxx zzz yyy jjj kkk\" | \"xxx zzz yyy mmm nnn\"",
		"( \"xxx zzz ccc ddd\" | \"xxx zzz yyy jjj kkk\" | \"xxx zzz yyy mmm nnn\" )",
		"( \"\"xxx zzz\" ( \"ccc ddd\" | \"yyy jjj kkk\" | \"yyy mmm nnn\" )\" )"
	);
}

TEST_F ( QueryParser, transform_common_phrases_8 )
{
	Transform (
		"\"aaa bbb ddd www xxx yyy zzz\" | \"aaa bbb eee www xxx yyy zzz\"",
		"( \"aaa bbb ddd www xxx yyy zzz\" | \"aaa bbb eee www xxx yyy zzz\" )",
		"( \"( \"aaa bbb ddd\" | \"aaa bbb eee\" ) \"www xxx yyy zzz\"\" )"
	);
}

TEST_F ( QueryParser, transform_common_phrases_9 )
{
	Transform (
		"\"www xxx yyy zzz ddd aaa bbb\" | \"www xxx yyy zzz eee aaa bbb\"",
		"( \"www xxx yyy zzz ddd aaa bbb\" | \"www xxx yyy zzz eee aaa bbb\" )",
		"( \"\"www xxx yyy zzz\" ( \"ddd aaa bbb\" | \"eee aaa bbb\" )\" )"
	);
}

TEST_F ( QueryParser, transform_common_phrases_10 )
{
	Transform (
		"\"xxx yyy zzz ddd\" | \"xxx yyy zzz eee\"",
		"( \"xxx yyy zzz ddd\" | \"xxx yyy zzz eee\" )",
		"( \"\"xxx yyy zzz\" ( ddd | eee )\" )"
	);
}

TEST_F ( QueryParser, transform_common_phrases_11 )
{
	Transform (
		"\"ddd xxx yyy zzz\" | \"eee xxx yyy zzz\"",
		"( \"ddd xxx yyy zzz\" | \"eee xxx yyy zzz\" )",
		"( \"( ddd | eee ) \"xxx yyy zzz\"\" )"
	);
}

// COMMON AND NOT FACTOR
TEST_F ( QueryParser, transform_common_and_not_factor_1 )
{
	Transform (
		"( aaa !xxx ) | ( aaa !yyy ) | ( aaa !zzz )",
		"( ( aaa AND NOT xxx ) | ( aaa AND NOT yyy ) | ( aaa AND NOT zzz ) )",
		"( aaa AND NOT ( xxx   yyy   zzz ) )"
	);
}

// different fields
TEST_F ( QueryParser, transform_different_fields )
{
	Transform (
		"( @title aaa @body !xxx ) | ( @body aaa @body !yyy )",
		"( ( ( @title: aaa ) AND NOT ( @body: xxx ) ) | ( ( @body: aaa ) AND NOT ( @body: yyy ) ) )",
		"( ( ( @title: aaa ) AND NOT ( @body: xxx ) ) | ( ( @body: aaa ) AND NOT ( @body: yyy ) ) )"
	);
}

TEST_F ( QueryParser, transform_common_and_not_factor_2 )
{
	Transform (
		"( aaa !xxx ) | ( aaa !yyy ) | ( aaa !zzz ) | ( bbb !xxx ) | ( bbb !yyy ) | ( bbb !zzz )",
		"( ( aaa AND NOT xxx ) | ( aaa AND NOT yyy ) | ( aaa AND NOT zzz ) | ( bbb AND NOT xxx ) | ( bbb AND NOT yyy ) | ( bbb AND NOT zzz ) )",
		"( ( aaa | bbb ) AND NOT ( xxx   yyy   zzz ) )"
	);
}

// COMMON AND NOT FACTOR WITH MIXED PHRASES/PROXIMITY terms
TEST_F ( QueryParser, transform_common_and_not_factor_with_mixed_phrases_1 )
{
	Transform (
		"( \"aaa bbb\"~10 !xxx ) | ( \"aaa bbb\"~20 !yyy ) | ( \"aaa bbb\" !zzz )",
		"( ( \"aaa bbb\"~10 AND NOT xxx ) | ( \"aaa bbb\"~20 AND NOT yyy ) | ( \"aaa bbb\" AND NOT zzz ) )",
		"( \"aaa bbb\"~20 AND NOT ( yyy   xxx   zzz ) )"
	);
}

// COMMON | NOT
TEST_F ( QueryParser, transform_common_or_not )
{
	Transform (
		"( aaa !(nnn | nnn1) ) | ( bbb !(nnn2 | nnn) )",
		"( ( aaa AND NOT ( nnn | nnn1 ) ) | ( bbb AND NOT ( nnn2 | nnn ) ) )",
		"( ( ( aaa AND NOT nnn1 ) | ( bbb AND NOT nnn2 ) ) AND NOT nnn )"
	);
}

// ExcessAndNot
TEST_F ( QueryParser, transform_excess_and_not )
{
	Transform (
		"( (aaa ( ( ( (fff (xxx !hhh)) !kkk ) ) bbb !ccc)) !ddd ) ( ( (zzz (xxx !vvv)) !kkk ) )",
		"( ( aaa   ( ( fff   ( xxx AND NOT hhh )   bbb ) AND NOT ( kkk | ccc ) )   ( ( zzz   ( xxx AND NOT vvv ) ) AND NOT kkk ) ) AND NOT ddd )",
		"( ( aaa   fff   xxx   bbb   zzz   xxx ) AND NOT ( vvv | hhh | kkk | kkk | ccc | ddd ) )"
	);
}


TEST_F ( QueryParser, transform_common_andnotfactor_root_1 )
{
	Transform (
		"(((0)""(0)(00 -000))|((0))|(-00\"00\")(0))",
		"( ( 00 AND NOT 000 ) | ( 00 AND NOT 00 ) )",
		"( 00 AND NOT ( 000   00 ) )"
	);
}

TEST_F ( QueryParser, fuzz_transform_common_andnotfactor_root_1 )
{
	Transform (
		"(((0)-00(00 -000))|((0))|(-00|\"00 00\"\"00\"))",
		"( ( ( 00 AND NOT 000 ) AND NOT 00 ) | ( 00 AND NOT ( 00 | \"00 00\" ) ) )",
		"( 00 AND NOT ( ( 000 | 00 )   00 ) )"
	);
}


TEST_F ( QueryParser, fuzz_non_executed_common_subterm )
{
	Transform (
		"aaa|(bbb(ccc|eee)(ddd|eee))",
//		"*|((!00(!(00!00|ccc!00)*)(0((!00|ccc!00)*)!00)|00(0(*!00)(00!00))*)|00\"\"*|00#*|00#*|00#*|00 *|00)",
		"( aaa | ( bbb   ( ccc | eee )   ( ddd | eee ) ) )",
		"( aaa | ( bbb   ( ccc | eee )   ( ddd | eee ) ) )",
		dFuzzerPseudoHits
	);
}

TEST_F ( QueryParser, fuzz_common_compound_not )
{
	Transform (
		"aaa|(bbb!(ccc ccc))",
		"( aaa | ( bbb AND NOT ( ccc   ccc ) ) )",
		"( aaa | ( bbb AND NOT ( ccc   ccc ) ) )",
		dFuzzerPseudoHits
	);
}


// COMMON | NOT WITH MIXED PHRASES/PROXIMITY terms
TEST_F ( QueryParser, transform_common_or_not_with_mixed_phrases )
{
	Transform (
		"( aaa !( \"jjj kkk\"~10 | (aaa|nnn) ) ) | ( bbb !( fff | \"jjj kkk\" ) ) | ( ccc !( (hhh kkk) | \"jjj kkk\"~20 ) )",
		"( ( aaa AND NOT ( \"jjj kkk\"~10 | ( aaa | nnn ) ) ) | ( bbb AND NOT ( fff | \"jjj kkk\" ) ) | ( ccc AND NOT ( ( hhh   kkk ) | \"jjj kkk\"~20 ) ) )",
		"( ( ( aaa AND NOT ( aaa | nnn ) ) | ( bbb AND NOT fff ) | ( ccc AND NOT ( hhh   kkk ) ) ) AND NOT \"jjj kkk\"~20 )"
	);
}

TEST_F ( QueryParser, query_mixed_fields_zones_relaxed_1 )
{
	Transform (
		"@@relaxed ZONESPAN:aaa bbb | @missed ddd | fff eee",
		"bbb",
		"bbb"
	);
}

TEST_F ( QueryParser, query_mixed_fields_zones_relaxed_2 )
{
	Transform (
		"@@relaxed aaa!bbb | @zzz c",
		"( aaa AND NOT bbb )",
		"( aaa AND NOT bbb )"
	);
}

TEST_F ( QueryParser, query_mixed_fields_zones_relaxed_3 )
{
	Transform (
		"@@relaxed @t r ( ?-?  | y @*xxx  | yyy )",
		"( xxx | yyy )",
		"( xxx | yyy )"
	);
}

TEST_F ( QueryParser, query_assert_from_fuzzer )
{
	CSphIndexSettings tTmpSettings;
	XQQuery_t tQuery;
	sphParseExtendedQuery ( tQuery, "aaa << !bbb ccc:x << !ddd | eee", nullptr, pTokenizer, &tSchema, pDict, tTmpSettings, nullptr );
}

TEST_F ( QueryParser, query_common_or_not_1 )
{
	Transform (
		"( aaa | ( bbb !( ccc | ccc ) ) )",
		"( aaa | ( bbb AND NOT ( ccc | ccc ) ) )",
		"( aaa | ( bbb AND NOT ( ccc | ccc ) ) )" );
}

TEST_F ( QueryParser, query_common_or_not_2 )
{
	Transform (
		"( ( aaa bbb ) ! ( ccc | ddd ) ) | ( ( eee ( fff ( aaa ! ccc ) ) ) ! ( ddd | ggg ) )"
		,"( ( ( aaa   bbb ) AND NOT ( ccc | ddd ) ) | ( ( eee   ( fff   ( aaa AND NOT ccc ) ) ) AND NOT ( ddd | ggg ) ) )"
		,"( ( ( ( aaa   bbb ) AND NOT ddd ) | ( ( eee   fff   aaa ) AND NOT ( ddd | ggg ) ) ) AND NOT ccc )"
	);
}


TEST_F ( QueryParser, query_common_or_not_3 )
{
	Transform (
		"((bod !bbb) |(a !bbb)llo !bab)|(aaa( hell ( bod !bbb) !bab) !bbq)"
		, "( ( ( ( bod AND NOT bbb )   llo ) AND NOT bab ) | ( ( aaa   ( ( hell   ( bod AND NOT bbb ) ) AND NOT bab ) ) AND NOT bbq ) )"
		, "( ( ( ( bod   llo ) AND NOT bbb ) | ( ( aaa   hell   bod ) AND NOT ( bbb | bbq ) ) ) AND NOT bab )"
	);
}


TEST_F ( QueryParser, multi_query_common_compoundnot )
{
	Transform (
		"aaa | ( ddag ! ( bbb ccc ccc ) )",
		"( aaa | ( ddag AND NOT ( bbb   ccc   ccc ) ) )",
		"( aaa | ( ddag AND NOT ( bbb   ccc   ccc ) ) )",
		dFuzzerPseudoHits
	);
}


TEST_F ( QueryParser, test_NOT )
{
	struct QueryTest_t
	{
		const char * m_sQuery;
		const char * m_sReconst;
		const bool m_bNotAllowed;
	};
	const QueryTest_t dCases[] =
	{
		{ "-one", "", false }
		, { "-one", "(  AND NOT one )", true }
	};

	for ( const auto & tCase : dCases )
	{
		AllowOnlyNot ( tCase.m_bNotAllowed );

		XQQuery_t tQuery;
		sphParseExtendedQuery ( tQuery, tCase.m_sQuery, nullptr, pTokenizer, &tSchema, pDict, tTmpSettings, nullptr );
		CSphString sReconst = sphReconstructNode ( tQuery.m_pRoot, &tSchema );
		ASSERT_STREQ ( sReconst.cstr(), tCase.m_sReconst );
	}
}

TEST ( Charsets, Merge_noIntersection )
{
	CSphVector<RemapRangeTagged_t> dRanges;
	AddRange ( { 100, 200, 103 }, dRanges );
	AddRange ( { 201, 300, 120 }, dRanges );
	MergeIntersectedRanges ( dRanges );
	EXPECT_EQ ( dRanges.GetLength(), 2 );
	auto dFoo = dRanges[0];
	EXPECT_EQ ( dFoo.m_iStart, 100 );
	EXPECT_EQ ( dFoo.m_iEnd, 200 );
	EXPECT_EQ ( dFoo.m_iRemapStart, 103 );
	dFoo = dRanges[1];
	EXPECT_EQ ( dFoo.m_iStart, 201 );
	EXPECT_EQ ( dFoo.m_iEnd, 300 );
	EXPECT_EQ ( dFoo.m_iRemapStart, 120 );
}

TEST ( Charsets, Merge_SameRange )
{
	CSphVector<RemapRangeTagged_t> dRanges;
	AddRange ( { 100, 200, 103 }, dRanges );
	AddRange ( { 100, 200, 120 }, dRanges );
	MergeIntersectedRanges ( dRanges );
	EXPECT_EQ ( dRanges.GetLength(), 1 );
	auto dFoo = dRanges[0];
	EXPECT_EQ ( dFoo.m_iStart, 100 );
	EXPECT_EQ ( dFoo.m_iEnd, 200 );
	EXPECT_EQ ( dFoo.m_iRemapStart, 120 );
}

TEST ( Charsets, Merge_FirstEndLonger )
{
	CSphVector<RemapRangeTagged_t> dRanges;
	AddRange ( { 100, 150, 102 }, dRanges );
	AddRange ( { 100, 200, 101 }, dRanges );
	MergeIntersectedRanges ( dRanges );
	EXPECT_EQ ( dRanges.GetLength(), 1 );
	auto dFoo = dRanges[0];
	EXPECT_EQ ( dFoo.m_iStart, 100 );
	EXPECT_EQ ( dFoo.m_iEnd, 200 );
	EXPECT_EQ ( dFoo.m_iRemapStart, 101 );
}

TEST ( Charsets, Merge_intersection )
{
	CSphVector<RemapRangeTagged_t> dRanges;
	AddRange ( { 100, 150, 101 }, dRanges );
	AddRange ( { 110, 160, 102 }, dRanges );
	MergeIntersectedRanges ( dRanges );
	EXPECT_EQ ( dRanges.GetLength(), 2 );
	auto dFoo = dRanges[0];
	EXPECT_EQ ( dFoo.m_iStart, 100 );
	EXPECT_EQ ( dFoo.m_iEnd, 109 );
	EXPECT_EQ ( dFoo.m_iRemapStart, 101 );
	dFoo = dRanges[1];
	EXPECT_EQ ( dFoo.m_iStart, 110 );
	EXPECT_EQ ( dFoo.m_iEnd, 160 );
	EXPECT_EQ ( dFoo.m_iRemapStart, 102 );
}

TEST ( Charsets, Merge_intersectionSameEnd )
{
	CSphVector<RemapRangeTagged_t> dRanges;
	AddRange ( { 100, 150, 101 }, dRanges );
	AddRange ( { 110, 150, 102 }, dRanges );
	MergeIntersectedRanges ( dRanges );
	EXPECT_EQ ( dRanges.GetLength(), 2 );
	auto dFoo = dRanges[0];
	EXPECT_EQ ( dFoo.m_iStart, 100 );
	EXPECT_EQ ( dFoo.m_iEnd, 109 );
	EXPECT_EQ ( dFoo.m_iRemapStart, 101 );
	dFoo = dRanges[1];
	EXPECT_EQ ( dFoo.m_iStart, 110 );
	EXPECT_EQ ( dFoo.m_iEnd, 150 );
	EXPECT_EQ ( dFoo.m_iRemapStart, 102 );
}

TEST ( Charsets, Merge_intersectionFullCover )
{
	CSphVector<RemapRangeTagged_t> dRanges;
	AddRange ( { 100, 160, 101 }, dRanges );
	AddRange ( { 110, 150, 102 }, dRanges );
	MergeIntersectedRanges ( dRanges );
	EXPECT_EQ ( dRanges.GetLength(), 3 );
	auto dFoo = dRanges[0];
	EXPECT_EQ ( dFoo.m_iStart, 100 );
	EXPECT_EQ ( dFoo.m_iEnd, 109 );
	EXPECT_EQ ( dFoo.m_iRemapStart, 101 );
	dFoo = dRanges[1];
	EXPECT_EQ ( dFoo.m_iStart, 110 );
	EXPECT_EQ ( dFoo.m_iEnd, 150 );
	EXPECT_EQ ( dFoo.m_iRemapStart, 102 );
	dFoo = dRanges[2];
	EXPECT_EQ ( dFoo.m_iStart, 151 );
	EXPECT_EQ ( dFoo.m_iEnd, 160 );
	EXPECT_EQ ( dFoo.m_iRemapStart, 152 );
}

TEST ( Charsets, ComplexMergeRanges )
{
	CSphVector<RemapRangeTagged_t> dRanges;
	AddRange ( { 100, 200, 103 }, dRanges );
	AddRange ( { 110, 190, 120 }, dRanges );
	AddRange ( { 120, 180, 121 }, dRanges );
	AddRange ( { 130, 170, 122 }, dRanges );
	AddRange ( { 140, 160, 123 }, dRanges );
	AddRange ( { 142, 158, 125 }, dRanges );
	AddRange ( { 144, 156, 127 }, dRanges );
	AddRange ( { 146, 170, 129 }, dRanges );
	AddRange ( { 150, 150, 124 }, dRanges );

	MergeIntersectedRanges ( dRanges );
	EXPECT_EQ ( dRanges.GetLength(), 10 );
	auto dFoo = dRanges[0];
	EXPECT_EQ ( dFoo.m_iStart, 100 );
	EXPECT_EQ ( dFoo.m_iEnd, 109 );
	EXPECT_EQ ( dFoo.m_iRemapStart, 103 );
	dFoo = dRanges[1];
	EXPECT_EQ ( dFoo.m_iStart, 110 );
	EXPECT_EQ ( dFoo.m_iEnd, 119 );
	EXPECT_EQ ( dFoo.m_iRemapStart, 120 );
	dFoo = dRanges[2];
	EXPECT_EQ ( dFoo.m_iStart, 120 );
	EXPECT_EQ ( dFoo.m_iEnd, 129 );
	EXPECT_EQ ( dFoo.m_iRemapStart, 121 );
	dFoo = dRanges[3];
	EXPECT_EQ ( dFoo.m_iStart, 130 );
	EXPECT_EQ ( dFoo.m_iEnd, 139 );
	EXPECT_EQ ( dFoo.m_iRemapStart, 122 );
	dFoo = dRanges[4];
	EXPECT_EQ ( dFoo.m_iStart, 140 );
	EXPECT_EQ ( dFoo.m_iEnd, 149 );
	EXPECT_EQ ( dFoo.m_iRemapStart, 123 );
	dFoo = dRanges[5];
	EXPECT_EQ ( dFoo.m_iStart, 150 );
	EXPECT_EQ ( dFoo.m_iEnd, 150 );
	EXPECT_EQ ( dFoo.m_iRemapStart, 124 );
	dFoo = dRanges[6];
	EXPECT_EQ ( dFoo.m_iStart, 151 );
	EXPECT_EQ ( dFoo.m_iEnd, 170 );
	EXPECT_EQ ( dFoo.m_iRemapStart, 134 );
	dFoo = dRanges[7];
	EXPECT_EQ ( dFoo.m_iStart, 171 );
	EXPECT_EQ ( dFoo.m_iEnd, 180 );
	EXPECT_EQ ( dFoo.m_iRemapStart, 172 );
	dFoo = dRanges[8];
	EXPECT_EQ ( dFoo.m_iStart, 181 );
	EXPECT_EQ ( dFoo.m_iEnd, 190 );
	EXPECT_EQ ( dFoo.m_iRemapStart, 191 );
	dFoo = dRanges[9];
	EXPECT_EQ ( dFoo.m_iStart, 191 );
	EXPECT_EQ ( dFoo.m_iEnd, 200 );
	EXPECT_EQ ( dFoo.m_iRemapStart, 194 );

}