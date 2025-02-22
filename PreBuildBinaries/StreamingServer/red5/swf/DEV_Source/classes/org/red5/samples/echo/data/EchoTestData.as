﻿package org.red5.samples.echo.data 
{
	/**
	 * RED5 Open Source Flash Server - http://www.osflash.org/red5
	 *
	 * Copyright (c) 2006-2008 by respective authors (see below). All rights reserved.
	 *
	 * This library is free software; you can redistribute it and/or modify it under the
	 * terms of the GNU Lesser General Public License as published by the Free Software
	 * Foundation; either version 2.1 of the License, or (at your option) any later
	 * version.
	 *
	 * This library is distributed in the hope that it will be useful, but WITHOUT ANY
	 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
	 * PARTICULAR PURPOSE. See the GNU Lesser General Public License for more details.
	 *
	 * You should have received a copy of the GNU Lesser General Public License along
	 * with this library; if not, write to the Free Software Foundation, Inc.,
	 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
	*/
	
	import flash.display.BitmapData;
	import flash.utils.ByteArray;
	import flash.xml.XMLDocument;
	
	import mx.collections.ArrayCollection;
	import mx.utils.ObjectProxy;
	
	import org.red5.utils.PNGEnc;
	
	/**
	 * Sample data for AMF0 and AMF3 data type tests.
	 * 
	 * @author Joachim Bauch ( jojo@struktur.de )
	 * @author Thijs Triemstra ( info@collab.nl )
	 */
	public class EchoTestData
	{
		private var _items		: Array = new Array();
		private var _amf0count 	: Number;
		
		/**
		 * @return Test values.
		 */		
		public function get items(): Array
		{
			return _items;
		}
		
		/**
		 * @return Number of AMF0 tests.
		 */		
		public function get AMF0COUNT(): Number
		{
			return _amf0count;
		}
		
		/**
		 * @param tests
		 */				
		public function EchoTestData( tests:Array )
		{
			// Add AMF0 specific tests below
			var amf0Tests:Array = tests[0];
			
			// null
			if ( amf0Tests[0].selected ) {
				_items.push(null);
			}
			// undefined
			if ( amf0Tests[1].selected ) {
				_items.push(undefined);
			}
			// Boolean
			if ( amf0Tests[2].selected ) {
				_items.push(true);
				_items.push(false);
			}
			// String
			if ( amf0Tests[3].selected ) {
				_items.push("");
				_items.push("Hello world!");
				var strings: Array = new Array();
				strings.push("test1");
				strings.push("test2");
				strings.push("test3");
				strings.push("test4");
				_items.push(strings);
				// long Strings
				var i: Number;
				var longString: String = "";
				// 10,000 chars
				for (i=0; i<1000; i++)
					longString = longString + "0123456789";
				_items.push(longString);
				// 100,000 chars
				var reallyLongString: String = "";
				for (i=0; i<10000; i++)
					reallyLongString = reallyLongString + "0123456789";
				_items.push(reallyLongString);
				// 1,000,000 chars
				var giganticString: String = "";
				for (i=0; i<100000; i++)
					giganticString = giganticString + "0123456789";
				_items.push(giganticString);
			}
			// Number
			if ( amf0Tests[4].selected ) {
				_items.push(0);
				_items.push(1);
				_items.push(-1);
				_items.push(256);
				_items.push(-256);
				_items.push(65536);
				_items.push(-65536);
				_items.push(Number.NaN);
				_items.push(Number.NEGATIVE_INFINITY);
				_items.push(Number.POSITIVE_INFINITY);
				_items.push(Number.MAX_VALUE);
				_items.push(Number.MIN_VALUE);
				_items.push(0.0);
				_items.push(1.5);
				_items.push(-1.5);
				_items.push(uint(0x000000));
			}
			// Arrays
			if ( amf0Tests[5].selected ) {
				_items.push(new Array());
				var tmp1: Array = new Array();
				tmp1.push(1);
				_items.push(tmp1);
				_items.push([1, 2]);
				_items.push([1, 2, 3]);
				_items.push([1, 2, [1, 2]]);
				var tmp2: Array = new Array();
				tmp2.push(1);
				tmp2[100] = 100;
				_items.push(tmp2);
				var tmp3: Array = new Array();
				tmp3.push(1);
				tmp3["one"] = 1;
				_items.push(tmp3);
			}
			// Object
			if ( amf0Tests[6].selected ) {
				var tmp4: Object = {a: "foo", b: "bar"};
				_items.push(tmp4);
				var tmp5: Array = new Array();
				tmp5.push(tmp4);
				tmp5.push(tmp4);
				_items.push(tmp5);
			}
			// Date
			if ( amf0Tests[7].selected ) {
				var now: Date = new Date();
				_items.push(now);
				var tmp6: Array = new Array();
				tmp6.push(now);
				tmp6.push(now);
				_items.push(tmp6);
			}
			// XML for ActionScript 1.0 and 2.0
			if ( amf0Tests[8].selected ) {
				var tmp7: XMLDocument = new XMLDocument(<employees>
	                    <employee ssn='123-123-1234'>
	                        <name first='John' last='Doe'/>
	                        <address>
	                            <street>11 Main St.</street>
	                            <city>San Francisco</city>
	                            <state>CA</state>
	                            <zip>98765</zip>
	                        </address>
	                    </employee>
	                    <employee ssn='789-789-7890'>
	                        <name first='Mary' last='Roe'/>
	                        <address>
	                            <street>99 Broad St.</street>
	                            <city>Newton</city>
	                            <state>MA</state>
	                            <zip>01234</zip>
	                        </address>
	                    </employee>
	                </employees>);
				_items.push(tmp7);
				var tmp8: Array = new Array();
				tmp8.push(tmp7);
				tmp8.push(tmp7);
				_items.push(tmp8);
			}
			// Custom class
			if ( amf0Tests[9].selected ) {
				var tmp9: EchoClass = new EchoClass();
				tmp9.attr1 = "one";
				tmp9.attr2 = 1;
				_items.push(tmp9);
				var tmp10: Array = new Array();
				tmp10.push(tmp9);
				tmp10.push(tmp9);
				_items.push(tmp10);
			}
			// Remote class
			if ( amf0Tests[10].selected ) {
				var remote: RemoteClass = new RemoteClass();
				remote.attribute1 = "one";
				remote.attribute2 = 2;
				_items.push(remote);
				var tmp11: Array = new Array();
				var remote1: RemoteClass = new RemoteClass();
				remote1.attribute1 = "one";
				remote1.attribute2 = 1;
				tmp11.push(remote1);
				var remote2: RemoteClass = new RemoteClass();
				remote2.attribute1 = "two";
				remote2.attribute2 = 2;
				tmp11.push(remote2);
				_items.push(tmp11);
				var remote3: RemoteClass = new RemoteClass();
				remote3.attribute1 = "three";
				remote3.attribute2 = 1234567890;
				_items.push(remote3);
				var remote4: RemoteClass = new RemoteClass();
				remote4.attribute1 = "four";
				remote4.attribute2 = 1185292800000;
				_items.push(remote4);
			}
			
			_amf0count = _items.length;
			
			// Add AMF3 specific tests below
			var amf3Tests:Array = tests[1];
			
			// XML for ActionScript 3.0
			if ( amf3Tests[0].selected ) {
				XML.ignoreComments = false;
				XML.ignoreProcessingInstructions = false;
				XML.prettyIndent = 0;
				XML.prettyPrinting = false;
				var customSettings:Object = XML.settings();
				var tmpXML:XML =
	                <employees>
	                    <employee ssn="123-123-1234">
	                        <name first="John" last="Doe"/>
	                        <address>
	                            <street>11 Main St.</street>
	                            <city>San Francisco</city>
	                            <state>CA</state>
	                            <zip>98765</zip>
	                        </address>
	                    </employee>
	                    <employee ssn="789-789-7890">
	                        <name first="Mary" last="Roe"/>
	                        <address>
	                            <street>99 Broad St.</street>
	                            <city>Newton</city>
	                            <state>MA</state>
	                            <zip>01234</zip>
	                        </address>
	                    </employee>
	                </employees>;
	            _items.push(tmpXML);
	            var tmp12: Array = new Array();
				tmp12.push(tmpXML);
				tmp12.push(tmpXML);
				_items.push(tmp12);
			}
			// Externalizable
			if ( amf3Tests[1].selected ) {
				var ext: ExternalizableClass = new ExternalizableClass();
				_items.push(ext);
				var tmp_1: Array = new Array();
				tmp_1.push(ext);
				tmp_1.push(ext);
				_items.push(tmp_1);
			}
			// ArrayCollection
			if ( amf3Tests[2].selected ) {
				var tmp13: ArrayCollection = new ArrayCollection();
				tmp13.addItem("one");
				tmp13.addItem(123);
				tmp13.addItem(null);
				_items.push(tmp13);
				var tmp_2: Array = new Array();
				tmp_2.push(tmp13);
				tmp_2.push(tmp13);
				_items.push(tmp_2);
			}
			// ObjectProxy
			if ( amf3Tests[3].selected ) {
				var temp14: ObjectProxy = new ObjectProxy( { a: "foo", b: 5 } );
				_items.push(temp14);
				var temp15: Array = new Array();
				temp15.push(temp14);
				temp15.push(temp14);
				_items.push(temp15);
			}
			// ByteArray
			if ( amf3Tests[4].selected ) {
				var bmp1:BitmapData = new BitmapData( 80, 80, false, 0xCCCCCC );
				// Draw a red line in a BitmapData object
				for (var g:uint = 0; g < 80; g++) {
				    var red:uint = 0xFF0000;
				    bmp1.setPixel( g, 40, red );
				}
				// Create ByteArray with PNG data
				var temp16: ByteArray = PNGEnc.encode( bmp1 );
				temp16.compress();
				_items.push(temp16);
				var tmp17: Array = new Array();
				tmp17.push(temp16);
				tmp17.push(temp16);
				_items.push(tmp17);
			}
			// Unsupported
			if ( amf3Tests[5].selected ) {
				var bmp2:BitmapData = new BitmapData( 80, 80, false, 0xCCCCCC );
				// Draw a blue line in a BitmapData object
				for (var h:uint = 0; h < 80; h++) {
				    var blue:uint = 0x3232CD;
				    bmp2.setPixel( h, 40, blue );
				}
				_items.push(bmp2);
				var tmp18: Array = new Array();
				tmp18.push(bmp2);
				tmp18.push(bmp2);
				_items.push(tmp18);
			}
		}
		
	}
}