/*
 * Copyright 2006 Robert Hanson <iamroberthanson AT gmail.com>
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *    http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package org.gwtwidgets.client.ui;

import org.gwtwidgets.client.ui.impl.PNGImageImpl;
import com.google.gwt.core.client.GWT;
import com.google.gwt.user.client.Event;
import com.google.gwt.user.client.ui.Image;


/**
 * Image widget that overcomes PNG browser incompatabilities.
 * Although meant for PNG images, it will work with any image
 * format supported by the browser.  If the image file ends
 * with ".png" or ".PNG" it will use the PNG specific routines,
 * otherwise will use generic non-PNG specific routines.
 * 
 * The URL, width, and height are required at the creation of the
 * widget, and may not be updated via the widget methogs.  Calling
 * setUrl() will throw a RuntimeException.  This is in part due to
 * the workarounds required for IE 5.5 and IE6.
 * 
 * @author rhanson
 */
public class PNGImage extends Image
{
    private PNGImageImpl impl;


    public PNGImage (String url, int width, int height)
    {
        impl = (PNGImageImpl) GWT.create(PNGImageImpl.class);
        
        setElement(impl.createElement(url, width, height));
        sinkEvents(Event.ONCLICK | Event.MOUSEEVENTS | Event.ONLOAD | Event.ONERROR);
    }

    
    public String getUrl ()
    {
        return impl.getUrl(this);
    }
   
    /**
     * Should not be used. Throws RuntimeException
     */
    public void setUrl (String url)
    {
        throw new RuntimeException("Not allowed to set url for a PNG image");
    }
}
