#pragma once

#include "TypeInfo.hpp"


namespace Skore
{
	class ReflectType;
	struct ArchiveReader;
	struct ArchiveWriter;


#define SK_CLASS(Type, BaseType)												\
	using Base = BaseType;														\
	virtual Skore::TypeID GetTypeId() const override {return Skore::TypeInfo<Type>::ID();}	\
	virtual bool IsBaseOf(Skore::TypeID typeId) override {if (BaseType::IsBaseOf(typeId)) return true; return typeId == Skore::TypeInfo<Type>::ID(); }


	class SK_API Object
	{
	public:
		virtual ~Object() = default;
		ReflectType* GetType() const;

		virtual bool IsBaseOf(TypeID typeId)
		{
			return typeId == TypeInfo<Object>::ID();
		}

		virtual TypeID GetTypeId() const
		{
			return TypeInfo<Object>::ID();
		}

		template <typename T>
		T* SafeCast()
		{
			if (this->IsBaseOf(TypeInfo<T>::ID()))
			{
				return static_cast<T*>(this);
			}
			return nullptr;
		}

		template <typename T>
		T* Cast()
		{
			return static_cast<T*>(this);
		}

		virtual VoidPtr GetInstance()
		{
			return this;
		}

		virtual void Serialize(ArchiveWriter& archiveWriter) const;
		virtual void Deserialize(ArchiveReader& archiveReader);
	};
}
