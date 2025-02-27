import os
import sys
sys.path.append('/opt/openoffice.org/basis3.0/program')
os.putenv('URE_BOOTSTRAP','vnd.sun.star.pathname:/opt/openoffice.org3/program/fundamentalrc')
#*************************************************************************
#
# DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
# 
# Copyright 2008 by Sun Microsystems, Inc.
#
# OpenOffice.org - a multi-platform office productivity suite
#
# $RCSfile: uno.py,v $
#
# $Revision: 1.9 $
#
# This file is part of OpenOffice.org.
#
# OpenOffice.org is free software: you can redistribute it and/or modify
# it under the terms of the GNU Lesser General Public License version 3
# only, as published by the Free Software Foundation.
#
# OpenOffice.org is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Lesser General Public License version 3 for more details
# (a copy is included in the LICENSE file that accompanied this code).
#
# You should have received a copy of the GNU Lesser General Public License
# version 3 along with OpenOffice.org.  If not, see
# <http://www.openoffice.org/license.html>
# for a copy of the LGPLv3 License.
#
#*************************************************************************
import sys
import pyuno

import __builtin__

# all functions and variables starting with a underscore (_) must be considered private
# and can be changed at any time. Don't use them
_g_ctx = pyuno.getComponentContext( )
_g_delegatee = __builtin__.__dict__["__import__"]

def getComponentContext():
    """ returns the UNO component context, that was used to initialize the python runtime.
    """ 
    return _g_ctx      

def getConstantByName( constant ):
    "Looks up the value of a idl constant by giving its explicit name"
    return pyuno.getConstantByName( constant )

def getTypeByName( typeName):
    """ returns a uno.Type instance of the type given by typeName. In case the
        type does not exist, a com.sun.star.uno.RuntimeException is raised.
    """ 
    return pyuno.getTypeByName( typeName )

def createUnoStruct( typeName, *args ):
    """creates a uno struct or exception given by typeName. The parameter args may
    1) be empty. In this case, you get a default constructed uno structure.
       ( e.g. createUnoStruct( "com.sun.star.uno.Exception" ) )
    2) be a sequence with exactly one element, that contains an instance of typeName.
       In this case, a copy constructed instance of typeName is returned
       ( e.g. createUnoStruct( "com.sun.star.uno.Exception" , e ) )
    3) be a sequence, where the length of the sequence must match the number of
       elements within typeName (e.g.
       createUnoStruct( "com.sun.star.uno.Exception", "foo error" , self) ). The
       elements with in the sequence must match the type of each struct element,
       otherwise an exception is thrown.
    """
    return getClass(typeName)( *args )

def getClass( typeName ):
    """returns the class of a concrete uno exception, struct or interface
    """
    return pyuno.getClass(typeName)

def isInterface( obj ):
    """returns true, when obj is a class of a uno interface"""
    return pyuno.isInterface( obj )

def generateUuid():
    "returns a 16 byte sequence containing a newly generated uuid or guid, see rtl/uuid.h "
    return pyuno.generateUuid()        

def systemPathToFileUrl( systemPath ):
    "returns a file-url for the given system path"
    return pyuno.systemPathToFileUrl( systemPath )

def fileUrlToSystemPath( url ):
    "returns a system path (determined by the system, the python interpreter is running on)"
    return pyuno.fileUrlToSystemPath( url )

def absolutize( path, relativeUrl ):
    "returns an absolute file url from the given urls"
    return pyuno.absolutize( path, relativeUrl )

def getCurrentContext():
    """Returns the currently valid current context.
       see http://udk.openoffice.org/common/man/concept/uno_contexts.html#current_context
       for an explanation on the current context concept
    """
    return pyuno.getCurrentContext()

def setCurrentContext( newContext ):
    """Sets newContext as new uno current context. The newContext must
    implement the XCurrentContext interface. The implemenation should
    handle the desired properties and delegate unknown properties to the
    old context. Ensure to reset the old one when you leave your stack ...
    see http://udk.openoffice.org/common/man/concept/uno_contexts.html#current_context
    """
    return pyuno.setCurrentContext( newContext )

        
class Enum:
    "Represents a UNO idl enum, use an instance of this class to explicitly pass a boolean to UNO" 
    #typeName the name of the enum as a string
    #value    the actual value of this enum as a string
    def __init__(self,typeName, value):
        self.typeName = typeName
        self.value = value
        pyuno.checkEnum( self )

    def __repr__(self):
        return "<uno.Enum %s (%r)>" % (self.typeName, self.value)

    def __eq__(self, that):
        if not isinstance(that, Enum):
            return False
        return (self.typeName == that.typeName) and (self.value == that.value)

class Type:
    "Represents a UNO type, use an instance of this class to explicitly pass a boolean to UNO"
#    typeName                 # Name of the UNO type
#    typeClass                # python Enum of TypeClass,  see com/sun/star/uno/TypeClass.idl
    def __init__(self, typeName, typeClass):
        self.typeName = typeName
        self.typeClass = typeClass
        pyuno.checkType(self)
    def __repr__(self):
        return "<Type instance %s (%r)>" % (self.typeName, self.typeClass)

    def __eq__(self, that):
        if not isinstance(that, Type):
            return False
        return self.typeClass == that.typeClass and self.typeName == that.typeName

    def __hash__(self):
        return self.typeName.__hash__()

class Bool(object):
    """Represents a UNO boolean, use an instance of this class to explicitly 
       pass a boolean to UNO.
       Note: This class is deprecated. Use python's True and False directly instead
    """
    def __new__(cls, value):
        if isinstance(value, (str, unicode)) and value == "true":
            return True
        if isinstance(value, (str, unicode)) and value == "false":
            return False
        if value:
            return True
        return False

class Char:
    "Represents a UNO char, use an instance of this class to explicitly pass a char to UNO"
    # @param value pass a Unicode string with length 1
    def __init__(self,value):
        assert isinstance(value, unicode)
        assert len(value) == 1
        self.value=value

    def __repr__(self):
        return "<Char instance %s>" % (self.value, )
        
    def __eq__(self, that):
        if isinstance(that, (str, unicode)):
            if len(that) > 1:
                return False
            return self.value == that[0]
        if isinstance(that, Char):        
            return self.value == that.value
        return False

# Suggested by Christian, but still some open problems which need to be solved first
#
#class ByteSequence(str):
#
#    def __repr__(self):
#        return "<ByteSequence instance %s>" % str.__repr__(self)

    # for a little bit compatitbility; setting value is not possible as 
    # strings are immutable
#    def _get_value(self):
#        return self
#
#    value = property(_get_value)        

class ByteSequence:
    def __init__(self, value):
        if isinstance(value, str):
            self.value = value
        elif isinstance(value, ByteSequence):
            self.value = value.value
        else:
            raise TypeError("expected string or bytesequence")

    def __repr__(self):
        return "<ByteSequence instance '%s'>" % (self.value, )

    def __eq__(self, that):
        if isinstance( that, ByteSequence):
            return self.value == that.value
        if isinstance(that, str):
            return self.value == that
        return False

    def __len__(self):
        return len(self.value)

    def __getitem__(self, index):
        return self.value[index]

    def __iter__( self ):
        return self.value.__iter__()

    def __add__( self , b ):
        if isinstance( b, str ):
            return ByteSequence( self.value + b )
        elif isinstance( b, ByteSequence ):
            return ByteSequence( self.value + b.value )
        raise TypeError( "expected string or ByteSequence as operand" )

    def __hash__( self ):
        return self.value.hash()


class Any:
    "use only in connection with uno.invoke() to pass an explicit typed any"
    def __init__(self, type, value ):
        if isinstance( type, Type ):
            self.type = type
        else:
            self.type = getTypeByName( type )
        self.value = value

def invoke( object, methodname, argTuple ):
    "use this function to pass exactly typed anys to the callee (using uno.Any)"
    return pyuno.invoke( object, methodname, argTuple )
    
#---------------------------------------------------------------------------------------
# don't use any functions beyond this point, private section, likely to change
#---------------------------------------------------------------------------------------
def _uno_import( name, *optargs ):
    try:
#       print "optargs = " + repr(optargs)
        if len(optargs) == 0:
           return _g_delegatee( name )
           #print _g_delegatee
        return _g_delegatee( name, *optargs )
    except ImportError:
        if len(optargs) != 3 or not optargs[2]:
           raise
    globals = optargs[0]
    locals = optargs[1]
    fromlist = optargs[2]
    modnames = name.split( "." )
    mod = None
    d = sys.modules
    for x in modnames:
        if d.has_key(x):
           mod = d[x]
        else:
           mod = pyuno.__class__(x)  # How to create a module ??
        d = mod.__dict__

    RuntimeException = pyuno.getClass( "com.sun.star.uno.RuntimeException" )
    for x in fromlist:
       if not d.has_key(x):
          if x.startswith( "typeOf" ):
             try: 
                d[x] = pyuno.getTypeByName( name + "." + x[6:len(x)] )
             except RuntimeException,e:
                raise ImportError( "type " + name + "." + x[6:len(x)] +" is unknown" )
          else:
            try:
                # check for structs, exceptions or interfaces
                d[x] = pyuno.getClass( name + "." + x )
            except RuntimeException,e:
                # check for enums 
                try:
                   d[x] = Enum( name , x )
                except RuntimeException,e2:
                   # check for constants
                   try:
                      d[x] = getConstantByName( name + "." + x )
                   except RuntimeException,e3:
                      # no known uno type !
                      raise ImportError( "type "+ name + "." +x + " is unknown" )
    return mod

# hook into the __import__ chain    
__builtin__.__dict__["__import__"] = _uno_import
        
# private function, don't use
def _impl_extractName(name):
    r = range (len(name)-1,0,-1)
    for i in r:
        if name[i] == ".":
           name = name[i+1:len(name)]
           break
    return name            

# private, referenced from the pyuno shared library
def _uno_struct__init__(self,*args):
    if len(args) == 1 and hasattr(args[0], "__class__") and args[0].__class__ == self.__class__ :
       self.__dict__["value"] = args[0]
    else:
       self.__dict__["value"] = pyuno._createUnoStructHelper(self.__class__.__pyunostruct__,args)
    
# private, referenced from the pyuno shared library
def _uno_struct__getattr__(self,name):
    return __builtin__.getattr(self.__dict__["value"],name)

# private, referenced from the pyuno shared library
def _uno_struct__setattr__(self,name,value):
    return __builtin__.setattr(self.__dict__["value"],name,value)

# private, referenced from the pyuno shared library
def _uno_struct__repr__(self):
    return repr(self.__dict__["value"])
    
def _uno_struct__str__(self):
    return str(self.__dict__["value"])

# private, referenced from the pyuno shared library
def _uno_struct__eq__(self,cmp):
    if hasattr(cmp,"value"):
       return self.__dict__["value"] == cmp.__dict__["value"]
    return False

# referenced from pyuno shared lib and pythonscript.py
def _uno_extract_printable_stacktrace( trace ):
    mod = None
    try:
        mod = __import__("traceback")
    except ImportError,e:
        pass
    ret = ""
    if mod:
        lst = mod.extract_tb( trace )
        max = len(lst)
        for j in range(max):
            i = lst[max-j-1]
            ret = ret + "  " + str(i[0]) + ":" + \
                  str(i[1]) + " in function " + \
                  str(i[2])  + "() [" + str(i[3]) + "]\n"
    else:
        ret = "Couldn't import traceback module"
    return ret
