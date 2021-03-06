/**
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <iostream>
#include <list>
#include <map>
#include <string>

#include <boost/variant.hpp>

// TODO(jsirois): Implement parsing that constructs JSON objects.


namespace JSON {

// Implementation of the JavaScript Object Notation (JSON) grammar
// using boost::variant. We explicitly define each "type" of the
// grammar, including 'true' (json::True), 'false' (json::False), and
// 'null' (json::Null), for clarity and also because boost::variant
// "picks" the wrong type when we try and use std::string, long (or
// int), double (or float), and bool all in the same variant (while it
// does work with explicit casts, it seemed bad style to force people
// to put those casts in place). We could have avoided using
// json::String or json::Number and just used std::string and double
// respectively, but we choose to include them for completeness
// (although, this does pay a 2x cost when compiling thanks to all the
// extra template instantiations).

struct String;
struct Number;
struct Object;
struct Array;
struct True;
struct False;
struct Null;


typedef boost::variant<boost::recursive_wrapper<String>,
                       boost::recursive_wrapper<Number>,
                       boost::recursive_wrapper<Object>,
                       boost::recursive_wrapper<Array>,
                       boost::recursive_wrapper<True>,
                       boost::recursive_wrapper<False>,
                       boost::recursive_wrapper<Null> > Value;


struct String
{
  String() {}
  String(const char* _value) : value(_value) {}
  String(const std::string& _value) : value(_value) {}
  std::string value;
};


struct Number
{
  Number() {}
  Number(double _value) : value(_value) {}
  double value;
};


struct Object
{
  std::map<std::string, Value> values;
};


struct Array
{
  std::list<Value> values;
};


struct True {};


struct False {};


struct Null {};


// Implementation of rendering JSON objects built above using standard
// C++ output streams. The visitor pattern is used thanks to to build
// a "renderer" with boost::static_visitor and two top-level render
// routines are provided for rendering JSON objects and arrays.

struct Renderer : boost::static_visitor<>
{
  Renderer(std::ostream& _out) : out(_out) {}

  void operator () (const String& string) const
  {
    out << "\"" << string.value << "\"";
  }

  void operator () (const Number& number) const
  {
    out.precision(10);
    out << number.value;
  }

  void operator () (const Object& object) const
  {
    out << "{";
    std::map<std::string, Value>::const_iterator iterator;
    iterator = object.values.begin();
    while (iterator != object.values.end()) {
      out << "\"" << (*iterator).first << "\":";
      boost::apply_visitor(Renderer(out), (*iterator).second);
      if (++iterator != object.values.end()) {
        out << ",";
      }
    }
    out << "}";
  }

  void operator () (const Array& array) const
  {
    out << "[";
    std::list<Value>::const_iterator iterator;
    iterator = array.values.begin();
    while (iterator != array.values.end()) {
      boost::apply_visitor(Renderer(out), *iterator);
      if (++iterator != array.values.end()) {
        out << ",";
      }
    }
    out << "]";
  }

  void operator () (const True&) const
  {
    out << "true";
  }

  void operator () (const False&) const
  {
    out << "false";
  }

  void operator () (const Null&) const
  {
    out << "null";
  }

private:
  std::ostream& out;
};


inline void render(std::ostream& out, const Object& object)
{
  Value value = object;
  boost::apply_visitor(Renderer(out), value);
}


inline void render(std::ostream& out, const Array& array)
{
  Value value = array;
  boost::apply_visitor(Renderer(out), value);
}

} // namespace JSON {
