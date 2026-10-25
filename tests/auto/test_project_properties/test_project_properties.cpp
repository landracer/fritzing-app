#define BOOST_TEST_MODULE PROJECT_PROPERTIES Tests
#include <boost/test/included/unit_test.hpp>

#include "project_properties.h"

#include <QDomDocument>

/*
Testing project_properties.cpp, a class for encapsulating project specific properties/settings.

Note on expectations: the current design (see src/project_properties.h: "Only the elements
defaulted in the constructor will be loaded") intentionally (1) returns an empty string for an
unknown/unknown-key lookup and (2) loads ONLY the keys defaulted in the constructor. These tests
were originally written against an older design where an unknown key defaulted to "no" and load()
read arbitrary elements; they were updated to assert the current, intentional behavior rather than
the removed legacy behavior.
*/

#include <boost/lexical_cast.hpp>
#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_CASE( project_properties_xml_empty )
{
	ProjectProperties projectProperties;

	QString xml = QString("<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
	QDomDocument doc;
	doc.setContent(xml, true);
	projectProperties.load(doc.documentElement());

	// Unknown key -> empty (no legacy "no" default).
	BOOST_CHECK_EQUAL(projectProperties.getProjectProperty("invalid").toStdString(), "");
}

BOOST_AUTO_TEST_CASE( project_properties_xml_no_prop )
{
	ProjectProperties projectProperties;

	QString xml = QString("<?xml version=\"1.0\" encoding=\"UTF-8\"?> <project_properties> </project_properties>");
	QDomDocument doc;
	doc.setContent(xml, true);
	projectProperties.load(doc.documentElement());

	BOOST_CHECK_EQUAL(projectProperties.getProjectProperty("invalid").toStdString(), "");
}


BOOST_AUTO_TEST_CASE( project_properties_xml_somefont )
{
	ProjectProperties projectProperties;

	// Unknown keys are intentionally NOT loaded (only constructor-defaulted keys are), so they read empty.
	QString xml = QString("<?xml version=\"1.0\" encoding=\"UTF-8\"?><project_properties><test1 value=\"yes\"/><test2 value=\"Somefont\"/></project_properties>");
	QDomDocument doc;
	doc.setContent(xml, true);
	projectProperties.load(doc.documentElement());

	BOOST_CHECK_EQUAL(projectProperties.getProjectProperty("test1").toStdString(), "");
	BOOST_CHECK_EQUAL(projectProperties.getProjectProperty("test2").toStdString(), "");
}

