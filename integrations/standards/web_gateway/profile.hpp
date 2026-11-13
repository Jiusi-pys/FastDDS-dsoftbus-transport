#pragma once
namespace c1 {
inline constexpr const char* type_catalog_xml=R"xml(<types xmlns="http://www.omg.org/ptc/2011/01/07/XML_Type_Representation/"><module name="Standards"><struct name="BoundedRecord"><member name="id" type="uint32" key="true"/><member name="sequence_number" type="uint32"/><member name="label" type="string" stringMaxLength="64"/><member name="payload" type="byte" sequenceMaxLength="256"/></struct></module></types>)xml";
}
