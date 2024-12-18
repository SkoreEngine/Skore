#pragma once

#include "Skore/Common.hpp"
#include "Allocator.hpp"
#include "Event.hpp"
#include "StringView.hpp"
#include "TypeInfo.hpp"
#include "String.hpp"
#include "SharedPtr.hpp"
#include "HashMap.hpp"
#include "Span.hpp"

namespace Skore
{
    class TypeBuilder;
    class ValueBuilder;
    class FunctionBuilder;
    class ConstructorBuilder;
    class FieldBuilder;
    class AttributeBuilder;
    class TypeHandler;
    struct FieldInfo;

    typedef VoidPtr (*FnCast)(const TypeHandler* typeHandler, VoidPtr derived);

    using OnTypeAdded = EventType<"Skore::OnTypeAdded"_h, void(const TypeHandler& typeHandler)>;


    template<typename T>
    struct ReleaseHandler
    {
        static void Release(T& value)
        {
        }
    };


    struct FunctionInfo
    {
        TypeID functionId{};
        TypeID ownerId{};
        usize paramCount{};
        const FieldInfo* paramsInfo{};
        const FieldInfo* returnInfo{};
    };

    struct FieldInfo
    {
        TypeID ownerId{};
        bool isConst{};
        bool isPointer{};
        bool isReference{};
        TypeInfo typeInfo{};
        usize offsetOf{};
    };

    template<typename Owner, typename Field>
    constexpr FieldInfo MakeFieldInfo()
    {
        return FieldInfo{
            .ownerId = GetTypeID<Owner>(),
            .isConst = false,
            .isPointer  = Traits::IsPointer<Field>,
            .isReference  = false,
            .typeInfo = GetTypeInfo<Field>()
        };
    }

    template<auto mfp, typename Owner, typename Field>
    constexpr FieldInfo MakeFieldInfo()
    {
        return FieldInfo{
            .ownerId = GetTypeID<Owner>(),
            .isConst = false,
            .isPointer  = Traits::IsPointer<Field>,
            .isReference  = false,
            .typeInfo = GetTypeInfo<Field>(),
            .offsetOf = Traits::OffsetOf(mfp)
        };
    }

    struct FunctionHandlerCreation
    {
        StringView            name{};
        TypeID                functionId{};
        TypeID                owner{};
        Span<const FieldInfo> params{};
        FieldInfo             retInfo{};
    };

    struct AttributeInfo
    {
        typedef ConstPtr(*FnGetValue)(const AttributeInfo* handler);
        typedef TypeInfo(*FnGetTypeInfo)(const AttributeInfo* handler);

        VoidPtr         userData{};

        FnGetValue      GetValue{};
        FnGetTypeInfo   GetInfo{};
    };

    class SK_API AttributeHandler
    {
    public:
        virtual ~AttributeHandler() = default;

        ConstPtr             GetAttribute(TypeID attributeId) const;
        bool                 HasAttribute(TypeID attributeId) const;
        Span<AttributeInfo*> GetAttributes() const;

        template<typename AttType>
        const AttType* GetAttribute() const
        {
            return static_cast<const AttType*>(GetAttribute(GetTypeID<AttType>()));
        }

        template<typename AttType>
        bool HasAttribute() const
        {
            return HasAttribute(GetTypeID<AttType>());
        }

        friend class AttributeBuilder;
    protected:
        virtual void OnAttributeCreated(TypeID attributeId) {};
    private:
        HashMap<TypeID, SharedPtr<AttributeInfo>>  m_attributes{};
        Array<AttributeInfo*>                      m_attributeArray{};
    };

    class SK_API ParamHandler : public AttributeHandler
    {
    public:
        ParamHandler(usize index, const FieldInfo& fieldInfo);
        const FieldInfo&    GetFieldInfo() const;
        const String&       GetName() const;
        void  SetName(const StringView& name);
    private:
        FieldInfo   m_fieldInfo{};
        String      m_name{};
    };

    class SK_API ValueHandler
    {
        typedef ConstPtr (*FnGetValue)(const ValueHandler* valueHandler);
        typedef i64 (*FnGetCode)(const ValueHandler* valueHandler);
        typedef bool (*FnCompare)(const ValueHandler* valueHandler, ConstPtr value);
        typedef void (*FnUpdate)(const ValueHandler* valueHandler, VoidPtr value);

    public:
        ValueHandler(const String& valueDesc);

        StringView GetDesc() const;
        ConstPtr   GetValue() const;
        i64        GetCode() const;
        bool       Compare(ConstPtr value) const;
        void       Update(VoidPtr value) const;

        friend class ValueBuilder;
    private:
        String     m_valueDesc{};
        FnGetValue m_fnGetValue{};
        FnGetCode  m_fnGetCode{};
        FnCompare  m_fnCompare{};
        FnUpdate  m_fnUpdate{};
    };


    class SK_API ConstructorHandler : public AttributeHandler
    {
    public:
        typedef void (*PlacementNewFn)(ConstructorHandler* handler, VoidPtr memory, VoidPtr* params);
        typedef VoidPtr (*NewInstanceFn)(ConstructorHandler* handler, Allocator& allocator, VoidPtr* params);
        typedef Object* (*NewObjectFn)(ConstructorHandler* handler, Allocator& allocator, VoidPtr* params);

        ConstructorHandler(FieldInfo* params,usize paramsCount);

        VoidPtr            NewInstance(Allocator& allocator, VoidPtr* params);
        Object*            NewObject(Allocator& allocator, VoidPtr* params);
        void               Construct(VoidPtr memory, VoidPtr* params);
        Span<ParamHandler> GetParams() const;

        friend class ConstructorBuilder;
    private:
        PlacementNewFn      placementNewFn;
        NewInstanceFn       newInstanceFn;
        NewObjectFn         newObjectFn;
        Array<ParamHandler> params{};
    };

    class SK_API FieldHandler : public AttributeHandler
    {
    public:

        typedef FieldInfo   (*FnGetFieldInfo)(const FieldHandler* fieldHandler);
        typedef VoidPtr     (*FnGetFieldPointer)(const FieldHandler* fieldHandler, VoidPtr instance);
        typedef void        (*FnCopyValueTo)(const FieldHandler* fieldHandler, ConstPtr instance, VoidPtr value);
        typedef void        (*FnSetValue)(const FieldHandler* fieldHandler, VoidPtr instance, ConstPtr value);

        FieldHandler(const String& name, TypeHandler& owner);

        StringView   GetName() const;
        FieldInfo    GetFieldInfo() const;
        VoidPtr      GetFieldPointer(VoidPtr instance) const;
        ConstPtr     GetFieldPointer(ConstPtr instance) const;
        void         CopyValueTo(ConstPtr instance, VoidPtr value) const;
        void         SetValue(VoidPtr instance, ConstPtr value) const;
        FnCast       GetOwnerCaster() const;
        TypeHandler& GetOwner() const;

        template<typename T>
        T& GetValueAs(VoidPtr instance)
        {
            return *static_cast<T*>(GetFieldPointer(instance));
        }

        template<typename T>
        void SetValueAs(VoidPtr instance, const T& value)
        {
            SetValue(instance, &value);
        }

        template<typename T>
        void CopyValueTo(ConstPtr instance, T& value)
        {
            CopyValueTo(instance, &value);
        }


        friend class FieldBuilder;
    private:
        String            name;
        TypeHandler&      owner;
        FnGetFieldInfo    fnGetFieldInfo{};
        FnGetFieldPointer fnGetFieldPointer{};
        FnCopyValueTo     fnCopyValueTo{};
        FnSetValue        fnSetValue{};
        FnCast            ownerCast{};
    };

    class SK_API FunctionHandler : public AttributeHandler
    {
        friend class FunctionBuilder;
        friend class TypeHandler;
    public:
        typedef void(*FnInvoke)(const FunctionHandler* handler, VoidPtr instance, VoidPtr ret, VoidPtr* params);
    private:

        String              m_name{};
        String              m_simpleName{};
        TypeID              m_functionId{U64_MAX};
        TypeHandler*        m_owner{};
        Array<ParamHandler> m_params{};
        FieldInfo           m_return{};
        FnInvoke            m_fnInvoke{};
        VoidPtr             m_functionPointer{};
        FnCast              m_ownerCast{};

        HashMap<TypeID, SharedPtr<AttributeHandler>> m_Attributes;
        Array<AttributeHandler*>                     m_AttributeArray;
    public:

        StringView          GetName() const;
        StringView          GetSimpleName() const;
        TypeID              GetFunctionId() const;
        Span<ParamHandler>  GetParams() const;
        FieldInfo           GetReturn() const;
        TypeHandler*        GetOwner() const;
        VoidPtr             GetFunctionPointer() const;
        FnInvoke            GetInvoker() const;
        FnCast              GetOwnerCaster() const;

        void Invoke(VoidPtr instance, VoidPtr ret, VoidPtr* params) const;

    protected:
        void OnAttributeCreated(TypeID attributeId) override;
    };

    struct DerivedType
    {
        TypeID typeId{};
        FnCast fnCast{};
    };


    class SK_API TypeHandler : public AttributeHandler
    {
    public:
        typedef void (*FnDestroy)(const TypeHandler* typeHandler, Allocator& allocator, VoidPtr instance);
        typedef void (*FnDestructor)(const TypeHandler* typeHandler, VoidPtr instance);
        typedef void (*FnBatchDestructor)(const TypeHandler* typeHandler, VoidPtr data, usize count);
        typedef void (*FnCopy)(const TypeHandler* typeHandler, ConstPtr source, VoidPtr dest);
        typedef void (*FnMove)(const TypeHandler* typeHandler, VoidPtr source, VoidPtr dest);
        typedef void (*FnRelease)(const TypeHandler* typeHandler, VoidPtr instance);
        friend class TypeBuilder;
    private:
        String            name{};
        String            simpleName{};
        TypeInfo          typeInfo{};
        u32               version{};
        FnDestroy         fnDestroy{};
        FnCopy            fnCopy{};
        FnDestructor      fnDestructor{};
        FnBatchDestructor fnBatchDestructor{};
        FnMove            fnMove{};
        FnRelease         fnRelease{};

        HashMap<usize, SharedPtr<ConstructorHandler>> constructors{};
        Array<ConstructorHandler*>                    constructorArray{};
        SharedPtr<ConstructorHandler>                 defaultConstructor{};
        HashMap<String, SharedPtr<FieldHandler>>      fields{};
        Array<FieldHandler*>                          fieldArray{};
        HashMap<String, SharedPtr<FunctionHandler>>   functions{};
        Array<FunctionHandler*>                       functionArray{};
        HashMap<String, SharedPtr<ValueHandler>>      values{};
        HashMap<i64, ValueHandler*>                   valuesByCode{};
        Array<ValueHandler*>                          valuesArray{};

        HashMap<TypeID, FnCast>                       baseTypes{};
        Array<TypeID>                                 baseTypesArray{};
        Array<DerivedType>                            derivedTypes{};
    public:
        TypeHandler(const StringView& name, const TypeInfo& typeInfo, u32 version);

        ConstructorHandler*             FindConstructor(TypeID* ids, usize size) const;
        Span<ConstructorHandler*>       GetConstructors() const;
        ConstructorHandler*             GetDefaultConstructor() const;

        FieldHandler*                   FindField(const StringView& fieldName) const;
        Span<FieldHandler*>             GetFields() const;

        FunctionHandler*                FindFunction(const StringView& functionName) const;
        Span<FunctionHandler*>          GetFunctions() const;

        ValueHandler*                   FindValueByName(const StringView& valueName) const;
        ValueHandler*                   FindValueByCode(i64 code) const;
        ValueHandler*                   FindValue(ConstPtr value) const;
        Span<ValueHandler*>             GetValues() const;

        Span<DerivedType>               GetDerivedTypes() const;
        Array<TypeID>                   GetBaseTypes() const;
        bool                            IsDerivedFrom(TypeID typeId) const;


        StringView          GetName() const;
        StringView          GetSimpleName() const;
        const TypeInfo&     GetTypeInfo() const;
        u32                 GetVersion() const;
        FnCast              GetCaster(TypeID typeId) const;

        void    Destroy(VoidPtr instance, Allocator& allocator = MemoryGlobals::GetDefaultAllocator()) const;
        void    Release(VoidPtr instance) const;
        void    Destructor(VoidPtr instance) const;
        void    BatchDestructor(VoidPtr data, usize count) const;
        void    Copy(ConstPtr source, VoidPtr dest) const;
        void    DeepCopy(ConstPtr source, VoidPtr dest) const;
        void    Move(VoidPtr source, VoidPtr dest) const;
        VoidPtr Cast(TypeID typeId, VoidPtr instance) const;

        VoidPtr NewInstance(Allocator& allocator = MemoryGlobals::GetDefaultAllocator()) const
        {
            if (ConstructorHandler* constructor = FindConstructor(nullptr, 0))
            {
                return constructor->NewInstance(allocator, nullptr);
            }
            return nullptr;
        }

        template<typename ...Args>
        VoidPtr NewInstance(Allocator& allocator, Args&& ...args) const
        {
            TypeID ids[] = {GetTypeID<Args>()...,};

            if (ConstructorHandler* constructor = FindConstructor(ids, sizeof...(args)))
            {
                VoidPtr params[] = {&args...};
                return constructor->NewInstance(allocator, params);
            }
            return nullptr;
        }

        template<typename ...Args>
        Object* NewObject(Args&& ...args) const
        {
            return NewObject(MemoryGlobals::GetDefaultAllocator(), Traits::Forward<Args>(args)...);
        }

        Object* NewObject(Allocator& allocator = MemoryGlobals::GetDefaultAllocator()) const
        {
            if (ConstructorHandler* constructor = FindConstructor(nullptr, 0))
            {
                return constructor->NewObject(allocator, nullptr);
            }
            return nullptr;
        }

        template<typename ...Args>
        Object* NewObject(Allocator& allocator, Args&& ...args) const
        {
            TypeID ids[] = {GetTypeID<Args>()...,};

            if (ConstructorHandler* constructor = FindConstructor(ids, sizeof...(args)))
            {
                VoidPtr params[] = {&args...};
                return constructor->NewObject(allocator, params);
            }
            return nullptr;
        }

        template<typename ...Args>
        VoidPtr NewInstance(Args&& ...args) const
        {
            return NewInstance(MemoryGlobals::GetDefaultAllocator(), Traits::Forward<Args>(args)...);
        }

        void Construct(VoidPtr memory) const
        {
            if (ConstructorHandler* constructor = FindConstructor(nullptr, 0))
            {
                return constructor->Construct(memory, nullptr);
            }
        }

        template<typename ...Args>
        void Construct(VoidPtr memory, Args&& ...args) const
        {
            TypeID ids[] = {GetTypeID<Args>()...,};
            if (ConstructorHandler* constructor = FindConstructor(ids, sizeof...(args)))
            {
                VoidPtr params[] = {&args...};
                constructor->Construct(memory, params);
            }
        }

        template<typename T>
        T* Cast(VoidPtr instance) const
        {
          return static_cast<T*>(Cast(GetTypeID<T>(), instance));
        }

    protected:
        void OnAttributeCreated(TypeID attributeId) override;
    };

    ///-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    ///-------------------------------------------------------------------------------------Builders----------------------------------------------------------------------------------------------------
    ///-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

    class SK_API AttributeBuilder
    {
    private:
        AttributeHandler& m_attributeHandler;
    public:
        explicit AttributeBuilder(AttributeHandler& attributeHandler);
        AttributeInfo& NewAttribute(TypeID attributeId);
    };

    class SK_API ValueBuilder
    {
    public:
        ValueBuilder(ValueHandler& valueHandler);

        void SetFnGetValue(ValueHandler::FnGetValue fnGetValue);
        void SerFnGetCode(ValueHandler::FnGetCode fnGetCode);
        void SetFnCompare(ValueHandler::FnCompare fnCompare);
        void SetFnUpdate(ValueHandler::FnUpdate fnUpdate);

    private:
        ValueHandler& valueHandler;
    };


    class SK_API ConstructorBuilder
    {
    private:
        ConstructorHandler& constructorHandler;
    public:
        ConstructorBuilder(ConstructorHandler& constructorHandler);

        void SetPlacementNewFn(ConstructorHandler::PlacementNewFn placementNew);
        void SetNewInstanceFn(ConstructorHandler::NewInstanceFn newInstance);
        void SetNewObjectFn(ConstructorHandler::NewObjectFn newObject);
    };

    class SK_API FieldBuilder
    {
    private:
        FieldHandler& fieldHandler;
    public:
        explicit FieldBuilder(FieldHandler& fieldHandler);

        FieldHandler& GetFieldHandler() const;

        void SetFnGetFieldInfo(FieldHandler::FnGetFieldInfo fnGetFieldInfo);
        void SetFnGetFieldPointer(FieldHandler::FnGetFieldPointer fnGetFieldPointer);
        void SetFnCopyValueTo(FieldHandler::FnCopyValueTo fnCopyValueTo);
        void SetFnSetValue(FieldHandler::FnSetValue fnSetValue);

        void Copy(const FieldHandler& fieldHandler, TypeHandler& owner);
    };

    class SK_API FunctionBuilder
    {
    private:
        FunctionHandler& m_functionHandler;
    public:
        explicit    FunctionBuilder(FunctionHandler& functionHandler);
        void        Create(const FunctionHandlerCreation& creation);
        void        Create(FunctionHandler& functionHandler, TypeHandler& owner);

        FunctionHandler& GetFunctionHandler() const;
        ParamHandler& GetParam(usize index);

        void SetFnInvoke(FunctionHandler::FnInvoke fnInvoke);
        void SetFunctionPointer(VoidPtr functionPointer);
    };

    class SK_API TypeBuilder
    {
    public:
        TypeBuilder(TypeHandler& typeHandler);

        void               SetFnDestroy(TypeHandler::FnDestroy fnDestroy);
        void               SetFnCopy(TypeHandler::FnCopy fnCopy);
        void               SetFnDestructor(TypeHandler::FnDestructor destructor);
        void               SetFnBatchDestructor(TypeHandler::FnBatchDestructor batchDestructor);
        void               SetFnMove(TypeHandler::FnMove fnMove);
        void               SetFnRelease(TypeHandler::FnRelease fnRelease);
        ConstructorBuilder NewConstructor(TypeID* ids, FieldInfo* params, usize size);
        FieldBuilder       NewField(const StringView& fieldName);
        FunctionBuilder    NewFunction(const FunctionHandlerCreation& creation);
        FunctionBuilder    NewFunction(StringView functionName);
        ValueBuilder       NewValue(const StringView& valueDesc, i64 code);
        void               AddBaseType(TypeID typeId, FnCast fnCast);
        void               Build() const;

        TypeHandler& GetTypeHandler() const;

    private:
        TypeHandler& typeHandler;
    };

    ///-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    ///------------------------------------------------------------------------------Native Handlers----------------------------------------------------------------------------------------------------
    ///-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

    template <class T, class = void>
    struct HasBase : Traits::FalseType
    {
    };

    template<class T>
    struct HasBase<T, Traits::VoidType<typename T::Bases>> : Traits::TrueType
    {
    };


    template<typename Base, typename Derived>
    struct TypeCaster
    {
        static VoidPtr Cast(const TypeHandler* typeHandler, VoidPtr derived)
        {
            return static_cast<Base*>(static_cast<Derived*>(derived));
        }
    };

    inline VoidPtr ForwardDerived(const TypeHandler* typeHandler, VoidPtr derived)
    {
        return derived;
    }

    template<typename Owner, typename Type>
    class NativeAttributeHandler
    {
    public:
        inline static Type Value{};

        NativeAttributeHandler(AttributeInfo& attributeInfo)
        {
            attributeInfo.GetValue = &GetValueImpl;
            attributeInfo.GetInfo = &GetTypeInfoImpl;
        }

    private:
        static ConstPtr GetValueImpl(const AttributeInfo* handler)
        {
            return &Value;
        }

        static TypeInfo GetTypeInfoImpl(const AttributeInfo* handler)
        {
            return GetTypeInfo<Type>();
        }
    };

    template<typename Owner, typename Type, typename Enable = void>
    struct NativeAttributeHandlerBuild
    {
        template<typename ...Args>
        static void Build(AttributeHandler& attributeHandler, Args&&... args)
        {
            AttributeBuilder builder {attributeHandler};
            NativeAttributeHandler<Owner, Type>::Value = Type{Traits::Forward<Args>(args)...};
            NativeAttributeHandler<Owner, Type>{builder.NewAttribute(GetTypeID<Type>())};

        }
    };

    template<typename Owner, typename Type>
    struct NativeAttributeHandlerBuild<Owner, Type, Traits::EnableIf<!Traits::IsComplete<Type>>>
    {
        template<typename ...Args>
        static void Build(AttributeHandler& attributeHandler, Args&&... args)
        {
        }
    };

    template<typename Owner>
    class NativeAttributeBuilder
    {
    public:
        explicit NativeAttributeBuilder(AttributeHandler& attributeHandler) : m_attributeHandler(attributeHandler)
        {
        }

        template<typename Type, typename ...Args>
        Owner& Attribute(Args&& ... args)
        {
            NativeAttributeHandlerBuild<Owner, Type>::Build(m_attributeHandler, Traits::Forward<Args>(args)...);
            return *static_cast<Owner*>(this);
        }
    private:
        AttributeHandler& m_attributeHandler;
    };


    template <usize I, typename Owner>
    class NativeParamHandler : public NativeAttributeBuilder<NativeParamHandler<I, Owner>>
    {
    public:
        NativeParamHandler(ParamHandler& paramHandler) : NativeAttributeBuilder<NativeParamHandler>(paramHandler), m_paramHandler(paramHandler)
        {
        }
    private:
        ParamHandler& m_paramHandler;
    };

    template<auto Value, typename Type, typename Owner, typename Enable = void>
    class NativeValue
    {
        static_assert(Traits::AlwaysFalse<Owner>, "Owner is not from a enum");
    };

    template<auto Value, typename Type, typename Owner>
    class NativeValue<Value, Type, Owner, Traits::EnableIf<Traits::IsEnum<Owner>>>
    {
    public:
        NativeValue(ValueBuilder valueBuilder)
        {
            valueBuilder.SetFnGetValue(&FnGetValueImpl);
            valueBuilder.SerFnGetCode(&FnGetCodeImpl);
            valueBuilder.SetFnCompare(&FnCompareImpl);
            valueBuilder.SetFnUpdate(&FnUpdateImpl);
        }
    private:
        static constexpr Type c_value = Value;

        static ConstPtr FnGetValueImpl(const ValueHandler* valueHandler)
        {
            return &c_value;
        }

        static i64 FnGetCodeImpl(const ValueHandler* valueHandler)
        {
            return static_cast<i64>(c_value);
        }

        static bool FnCompareImpl(const ValueHandler* valueHandler, ConstPtr value)
        {
            return *static_cast<const Type*>(valueHandler->GetValue()) == *static_cast<const Type*>(value);
        }

        static void FnUpdateImpl(const ValueHandler* valueHandler, VoidPtr value)
        {
            *static_cast<Type*>(value) = c_value;
        }
    };


    template<typename Owner, typename ...Args>
    class NativeConstructorHandler
    {
    public:
        explicit NativeConstructorHandler(ConstructorBuilder constructorBuilder) : m_constructorHandler(constructorBuilder)
        {
            constructorBuilder.SetNewInstanceFn(&NewInstanceImpl);
            constructorBuilder.SetNewObjectFn(NewObjectImpl);
            constructorBuilder.SetPlacementNewFn(PlacementNewImpl);
        }

    private:
        ConstructorBuilder m_constructorHandler;

        template<typename ...Vals>
        static void Eval(Owner* owner, VoidPtr* params, Traits::IndexSequence<>, Vals&& ...vals)
        {
            if constexpr (Traits::IsAggregate<Owner>)
            {
                new(PlaceHolder(), owner) Owner{Traits::Forward<Vals>(vals)...};
            }
            else
            {
                new(PlaceHolder(), owner) Owner(Traits::Forward<Vals>(vals)...);
            }
        }

        template<typename T, typename ...Tp, usize I, usize... Is, typename ...Vls>
        static void Eval(Owner* owner, VoidPtr* params, Traits::IndexSequence<I, Is...> seq, Vls&& ...vls)
        {
            return Eval<Tp...>(owner, params, Traits::IndexSequence<Is...>(), Traits::Forward<Vls>(vls)..., *static_cast<T*>(params[I]));
        }

        static void PlacementNewImpl(ConstructorHandler* handler, VoidPtr memory, VoidPtr* params)
        {
            const usize size = sizeof...(Args);
            Eval<Args...>(static_cast<Owner*>(memory), params, Traits::MakeIntegerSequence<usize, size>{});
        }

        static VoidPtr NewInstanceImpl(ConstructorHandler* handler, Allocator& allocator, VoidPtr* params)
        {
            Owner* ptr = static_cast<Owner*>(allocator.MemAlloc(sizeof(Owner), alignof(Owner)));
            PlacementNewImpl(handler, ptr, params);
            return ptr;
        }

        static Object* NewObjectImpl(ConstructorHandler* handler, Allocator& allocator, VoidPtr* params)
        {
            if constexpr (Traits::IsBaseOf<Object, Owner>)
            {
                Owner* ptr = static_cast<Owner*>(allocator.MemAlloc(sizeof(Owner), alignof(Owner)));
                PlacementNewImpl(handler, ptr, params);
                return ptr;
            }
            return nullptr;
        }
    };


    //fields

    template<auto mfp, typename Owner, typename Field>
    class NativeFieldHandlerBase : public NativeAttributeBuilder<NativeFieldHandlerBase<mfp, Owner, Field>>
    {
    protected:
        static_assert(Traits::IsComplete<Traits::RemoveAll<Field>>, "fields cannot be incomplete");

        explicit NativeFieldHandlerBase(FieldBuilder fieldBuilder) : NativeAttributeBuilder<NativeFieldHandlerBase>(fieldBuilder.GetFieldHandler())
        {
            fieldBuilder.SetFnGetFieldInfo(&GetFieldImpl);
            fieldBuilder.SetFnGetFieldPointer(&FnGetFieldPointerImpl);
        }

        static FieldInfo GetFieldImpl(const FieldHandler* fieldHandler)
        {
            return MakeFieldInfo<mfp, Owner, Field>();
        }

        static VoidPtr FnGetFieldPointerImpl(const FieldHandler* fieldHandler, VoidPtr instance)
        {
            instance = fieldHandler->GetOwnerCaster()(&fieldHandler->GetOwner(), instance);
            return &(*static_cast<Owner*>(instance).*mfp);
        }
    };

    template<auto mfp, auto getFp, auto setFp, typename Owner, typename Field>
    class NativeFieldHandler : public NativeFieldHandlerBase<mfp, Owner, Field>
    {
    public:
        explicit NativeFieldHandler(FieldBuilder fieldBuilder): NativeFieldHandlerBase<mfp, Owner, Field>(fieldBuilder)
        {
            fieldBuilder.SetFnSetValue(&SetValue);
            fieldBuilder.SetFnCopyValueTo(&CopyValueToImpl);
        }
    private:
        static void CopyValueToImpl(const FieldHandler* fieldHandler, ConstPtr instance, VoidPtr value)
        {
            using Return = Traits::FunctionReturnType<getFp>;
            instance = fieldHandler->GetOwnerCaster()(&fieldHandler->GetOwner(), const_cast<VoidPtr>(instance));
            *static_cast<Traits::RemoveConstRef<Return>*>(value) = (*static_cast<const Owner*>(instance).*getFp)();
        }

        static void SetValue(const FieldHandler* fieldHandler, VoidPtr instance, ConstPtr value)
        {
            instance = fieldHandler->GetOwnerCaster()(&fieldHandler->GetOwner(), instance);
            (*static_cast<Owner*>(instance).*setFp)(*static_cast<const Field*>(value));
        }
    };

    template <auto mfp, typename Owner, typename Field>
    class NativeFieldHandler<mfp, nullptr, nullptr, Owner, Field> : public NativeFieldHandlerBase<mfp, Owner, Field>
    {
    public:
        explicit NativeFieldHandler(FieldBuilder fieldBuilder): NativeFieldHandlerBase<mfp, Owner, Field>(fieldBuilder)
        {
            fieldBuilder.SetFnCopyValueTo(&CopyValueToImpl);
            fieldBuilder.SetFnSetValue(&SetValue);
        }
    private:
        static void CopyValueToImpl(const FieldHandler* fieldHandler, ConstPtr instance, VoidPtr value)
        {
            if constexpr(Traits::IsCopyConstructible<Field>)
            {
                instance = fieldHandler->GetOwnerCaster()(&fieldHandler->GetOwner(), const_cast<VoidPtr>(instance));
                *static_cast<Field*>(value) = *static_cast<const Owner*>(instance).*mfp;
            }
        }

        static void SetValue(const FieldHandler* fieldHandler, VoidPtr instance, ConstPtr value)
        {
            if constexpr(Traits::IsCopyConstructible<Field>)
            {
                instance = fieldHandler->GetOwnerCaster()(&fieldHandler->GetOwner(), instance);
                *static_cast<Owner*>(instance).*mfp = *static_cast<const Field*>(value);
            }
        }
    };


    template<auto mfp, auto getFp, auto setFp, typename Field>
    struct FieldTemplateDecomposer
    {
    };

    template<auto mfp, auto getFp, auto setFp, typename Owner, typename FieldType>
    struct FieldTemplateDecomposer<mfp, getFp, setFp, FieldType Owner::*>
    {

        static TypeID GetTypeId()
        {
            return GetTypeID<FieldType>();
        }

        static auto CreateHandler(FieldBuilder fieldBuilder)
        {
            return NativeFieldHandler<mfp, getFp, setFp, Owner, FieldType>(fieldBuilder);
        }
    };


    //functions

    template<auto fp, typename Return, typename ...Args>
    class NativeFunctionHandler : public NativeAttributeBuilder<NativeFunctionHandler<fp, Return, Args...>>
    {
    public:
        NativeFunctionHandler(FunctionBuilder functionBuilder) :
            NativeAttributeBuilder<NativeFunctionHandler>(functionBuilder.GetFunctionHandler()),
            m_functionBuilder(functionBuilder)
        {
            functionBuilder.SetFnInvoke(InvokeImpl);
            functionBuilder.SetFunctionPointer(reinterpret_cast<VoidPtr>(&FunctionImpl));
        }

        template <usize I>
        auto Param(const StringView& name = "")
        {
            ParamHandler& paramHandler = m_functionBuilder.GetParam(I);
            if (!name.Empty())
            {
                paramHandler.SetName(name);
            }
            return NativeParamHandler<I, NativeFunctionHandler>(paramHandler);
        }

    private:
        FunctionBuilder m_functionBuilder;

        static void InvokeImpl(const FunctionHandler* handler, VoidPtr instance, VoidPtr ret, VoidPtr* params)
        {
            u32 i{sizeof...(Args)};
            if constexpr (Traits::IsSame<Return, void>)
            {
                fp(*static_cast<Traits::RemoveReference<Args>*>(params[--i])...);
            }
            else
            {
                *static_cast<Traits::RemoveReference<Return>*>(ret) = fp(*static_cast<Traits::RemoveReference<Args>*>(params[--i])...);
            }
        }

        static Return FunctionImpl(const FunctionHandler* handler, VoidPtr  instance, Args... args)
        {
            return fp(args...);
        }
    };

    template<auto mfp, typename Return, typename Owner, typename ...Args>
    class NativeMemberFunctionHandler : public NativeAttributeBuilder<NativeMemberFunctionHandler<mfp, Return, Owner, Args...>>
    {
    private:
        FunctionBuilder m_functionBuilder;
    public:
        explicit NativeMemberFunctionHandler(FunctionBuilder& functionBuilder) : NativeAttributeBuilder<NativeMemberFunctionHandler>(functionBuilder.GetFunctionHandler()),
            m_functionBuilder(functionBuilder)
        {
            functionBuilder.SetFnInvoke(InvokeImpl);
            functionBuilder.SetFunctionPointer(reinterpret_cast<VoidPtr>(&FunctionImpl));
        }
    private:
        static void InvokeImpl(const FunctionHandler* handler, VoidPtr instance, VoidPtr ret, VoidPtr * params)
        {
            FnCast ownerCaster = handler->GetOwnerCaster();
            instance = ownerCaster(handler->GetOwner(), instance);

            u32 i{sizeof...(Args)};
            if constexpr (Traits::IsSame<Return,void>)
            {
                (static_cast<Owner*>(instance)->*mfp)(*static_cast<Traits::RemoveReference<Args>*>(params[--i])...);
            }
            else
            {
                *static_cast<Traits::RemoveReference<Return>*>(ret) = (static_cast<Owner*>(instance)->*mfp)(*static_cast<Traits::RemoveReference<Args>*>(params[--i])...);
            }
        }

        static Return FunctionImpl(const FunctionHandler* handler, VoidPtr instance, Args... args)
        {
            FnCast ownerCaster = handler->GetOwnerCaster();
            instance = ownerCaster(handler->GetOwner(), instance);

            return (static_cast<Owner*>(instance)->*mfp)(args...);
        }
    };

    template<auto func, typename Function>
    struct MemberFunctionTemplateDecomposer
    {
    };

    template<auto mfp, typename Return, typename Owner>
    struct MemberFunctionTemplateDecomposer<mfp, Return(Owner::*)()>
    {
        using FuncType = MemberFunctionTemplateDecomposer;

        static decltype(auto) CreateHandler(FunctionBuilder functionBuilder)
        {
            return NativeMemberFunctionHandler<mfp, Return, Owner>(functionBuilder);
        }

        static FunctionHandlerCreation MakeCreation(const StringView& name)
        {
            return FunctionHandlerCreation{
                .name = name,
                .functionId = GetTypeID<FuncType>(),
                .owner = GetTypeID<Owner>(),
                .retInfo = MakeFieldInfo<Owner, Return>()
            };
        }
    };

    template<auto mfp, typename Return, typename Owner, typename ...Args>
    struct MemberFunctionTemplateDecomposer<mfp, Return(Owner::*)(Args...)>
    {
        using FuncType = MemberFunctionTemplateDecomposer<mfp, Return(Owner::*)()>;
        static constexpr FieldInfo Params[] = {MakeFieldInfo<Owner, Args>()...};

        static decltype(auto) CreateHandler(FunctionBuilder functionBuilder)
        {
            return NativeMemberFunctionHandler<mfp, Return, Owner, Args...>(functionBuilder);
        }

        static FunctionHandlerCreation MakeCreation(const StringView& name)
        {
            return FunctionHandlerCreation{
                .name = name,
                .functionId = GetTypeID<FuncType>(),
                .owner = GetTypeID<Owner>(),
                .params = {Params, sizeof...(Args)},
                .retInfo = MakeFieldInfo<Owner, Return>()
            };
        }
    };

    template<auto fp, typename Return>
    struct MemberFunctionTemplateDecomposer<fp, Return(*)()>
    {
        using FuncType = MemberFunctionTemplateDecomposer;

        static decltype(auto) CreateHandler(FunctionBuilder functionBuilder)
        {
            return NativeFunctionHandler<fp, Return>(functionBuilder);
        }

        static FunctionHandlerCreation MakeCreation(const StringView& name)
        {
            return FunctionHandlerCreation{
                .name = name,
                .functionId = GetTypeID<FuncType>(),
                .retInfo = MakeFieldInfo<void, Return>()
            };
        }
    };

    template<auto fp, typename Return, typename ...Args>
    struct MemberFunctionTemplateDecomposer<fp, Return(*)(Args...)>
    {
        using FuncType = MemberFunctionTemplateDecomposer<fp, Return(*)(Args...)>;
        static constexpr FieldInfo Params[] = {MakeFieldInfo<void, Args>()...};

        static decltype(auto) CreateHandler(FunctionBuilder functionBuilder)
        {
            return NativeFunctionHandler<fp, Return, Args...>(functionBuilder);
        }

        static FunctionHandlerCreation MakeCreation(const StringView& name)
        {
            return FunctionHandlerCreation{
                .name = name,
                .functionId = GetTypeID<FuncType>(),
                .params = {Params, sizeof...(Args)},
                .retInfo = MakeFieldInfo<void, Return>()
            };
        }
    };


    //type

    template<typename Type, typename Enabler = void>
    struct NativeTypeHandlerFuncs
    {
        static void DestroyImpl(const TypeHandler* typeHandler, Allocator& allocator, VoidPtr instance){}
        static void CopyImpl(const TypeHandler* typeHandler, ConstPtr source, VoidPtr dest) {}
        static void DestructorImpl(const TypeHandler* typeHandler, VoidPtr instance){}
        static void BatchDestructorImpl(const TypeHandler* typeHandler, VoidPtr data, usize count) {};
        static void MoveImpl(const TypeHandler* typeHandler, VoidPtr origin, VoidPtr destination) {}
        static void ReleaseImpl(const TypeHandler* typeHandler, VoidPtr instance) {}
    };

    template<typename Type>
    struct NativeTypeHandlerFuncs<Type, Traits::VoidType<decltype(sizeof(Type) != 0)>>
    {
        static void DestroyImpl(const TypeHandler* typeHandler, Allocator& allocator, VoidPtr instance)
        {
            if constexpr (Traits::IsDestructible<Type>)
            {
                static_cast<Type*>(instance)->~Type();
            }
            allocator.MemFree(instance);
        }

        static void CopyImpl(const TypeHandler* typeHandler, ConstPtr source, VoidPtr dest)
        {
            if constexpr (Traits::IsCopyConstructible<Type>)
            {
                new(PlaceHolder(), dest) Type(*static_cast<const Type*>(source));
            }
        }

        static void DestructorImpl(const TypeHandler* typeHandler, VoidPtr instance)
        {
            if constexpr (Traits::IsDestructible<Type>)
            {
                static_cast<Type*>(instance)->~Type();
            }
        };

        static void BatchDestructorImpl(const TypeHandler* typeHandler, VoidPtr data, usize count)
        {
            if constexpr (Traits::IsDestructible<Type>)
            {
                Type* arr = static_cast<Type*>(data);
                for (int i = 0; i < count; ++i)
                {
                    arr[i].~Type();
                }
            }
        }

        static void MoveImpl(const TypeHandler* typeHandler, VoidPtr origin, VoidPtr destination)
        {
            if constexpr (Traits::IsMoveConstructible<Type>)
            {
                new(PlaceHolder(), destination) Type{std::move(*static_cast<Type*>(origin))};
            }
            else if constexpr (Traits::IsCopyConstructible<Type>)
            {
                new(PlaceHolder(), destination) Type{*static_cast<const Type*>(origin)};
            }
            else
            {
                SK_ASSERT(false, "type is not move or copy constructible");
            }
        }

        static void ReleaseImpl(const TypeHandler* typeHandler, VoidPtr instance)
        {
            ReleaseHandler<Type>::Release(*static_cast<Type*>(instance));
        }
    };


    template<typename Type>
    class NativeTypeHandler : public NativeAttributeBuilder<NativeTypeHandler<Type>>
    {
    private:
        TypeBuilder m_typeBuilder;
    public:
        explicit NativeTypeHandler(TypeBuilder typeBuilder) : NativeAttributeBuilder<NativeTypeHandler>(typeBuilder.GetTypeHandler()), m_typeBuilder(typeBuilder)
        {
            if constexpr (Traits::IsDirectConstructible<Type>)
            {
                this->Constructor();
            }
            typeBuilder.SetFnDestroy(&NativeTypeHandlerFuncs<Type>::DestroyImpl);
            typeBuilder.SetFnCopy(&NativeTypeHandlerFuncs<Type>::CopyImpl);
            typeBuilder.SetFnDestructor(&NativeTypeHandlerFuncs<Type>::DestructorImpl);
            typeBuilder.SetFnBatchDestructor(&NativeTypeHandlerFuncs<Type>::BatchDestructorImpl);
            typeBuilder.SetFnMove(&NativeTypeHandlerFuncs<Type>::MoveImpl);
            typeBuilder.SetFnRelease(&NativeTypeHandlerFuncs<Type>::ReleaseImpl);
        }

        auto Constructor()
        {
            return NativeConstructorHandler<Type>(m_typeBuilder.NewConstructor(nullptr, nullptr, 0));
        }

        template<typename ...Args>
        auto Constructor()
        {
            FieldInfo params[] = {MakeFieldInfo<void, Args>()...};
            TypeID ids[] = {GetTypeID<Args>()...,};
            return NativeConstructorHandler<Type, Args...>(m_typeBuilder.NewConstructor(ids, params, sizeof...(Args)));
        }

        template<auto mfp>
        auto Field(const StringView& name)
        {
            using FieldDecomp = FieldTemplateDecomposer<mfp, nullptr, nullptr, decltype(mfp)>;
            return FieldDecomp::CreateHandler(m_typeBuilder.NewField(name));
        }

        template<auto mfp, auto getFp, auto setFp>
        auto Field(const StringView& fieldName)
        {
            using FieldDecomp = FieldTemplateDecomposer<mfp, getFp, setFp, decltype(mfp)>;
            return FieldDecomp::CreateHandler(m_typeBuilder.NewField(fieldName));
        }


        template<auto mfp>
        auto Function(const StringView& name)
        {
            using FuncType = Traits::RemoveConstFunc<decltype(mfp)>;
            using DecompType = MemberFunctionTemplateDecomposer<mfp, FuncType>;
            return DecompType::CreateHandler(m_typeBuilder.NewFunction(DecompType::MakeCreation(name)));
        }

        template <auto value>
        auto Value(const StringView& valueName)
        {
            return NativeValue<value, decltype(value), Type>(m_typeBuilder.NewValue(valueName, static_cast<i64>(value)));
        }

        ~NativeTypeHandler()
        {
            m_typeBuilder.Build();
        }
    };

    template<typename, typename = void>
    struct HasRegisterTypeImpl : Traits::FalseType
    {
    };

    template<typename T>
    struct HasRegisterTypeImpl<T, Traits::VoidType<decltype(static_cast<void(*)(NativeTypeHandler<T>&)>(&T::RegisterType))>> : Traits::TrueType
    {
    };

    template<typename T>
    constexpr bool HasRegisterType = HasRegisterTypeImpl<T>::value;


    template<typename Type, typename DerivedType, typename Bases>
    struct AddBaseTypes
    {
    };

    template <typename Type, typename DerivedType, typename... TypeBases>
    struct AddBaseTypes<Type, DerivedType, BaseTypes<TypeBases...>>
    {
        template <typename Base>
        static void CheckBase(TypeBuilder& typeBuilder)
        {
            if constexpr (HasBase<Base>::value)
            {
                AddBaseTypes<Base, DerivedType, typename Base::Bases>::Register(typeBuilder);
            }
        }

        static void Register(TypeBuilder& typeBuilder)
        {
            (typeBuilder.AddBaseType(GetTypeID<TypeBases>(), TypeCaster<TypeBases, DerivedType>::Cast), ...);
            (CheckBase<TypeBases>(typeBuilder), ...);
        }
    };


    namespace Registry
    {
        SK_API TypeBuilder        NewType(const StringView& name, const TypeInfo& typeInfo);
        SK_API TypeHandler*       FindTypeByName(const StringView& name);
        SK_API TypeHandler*       FindTypeById(TypeID typeId);
        SK_API Span<TypeHandler*> FindTypesByAttribute(TypeID typeId);


        SK_API FunctionBuilder          NewFunction(const FunctionHandlerCreation& functionHandlerCreation);
        SK_API FunctionHandler*         FindFunctionByName(const StringView& name);
        SK_API Span<FunctionHandler*>   FindFunctionsByAttribute(TypeID typeId);


        template<typename T>
        decltype(auto) Type(const StringView& name)
        {
            TypeBuilder typeBuilder = NewType(name, GetTypeInfo<T>());

            if constexpr (HasBase<T>::value)
            {
                AddBaseTypes<T, T, typename T::Bases>::Register(typeBuilder);
            }

            NativeTypeHandler<T> handler = NativeTypeHandler<T>(typeBuilder);

            if constexpr (HasRegisterType<T>)
            {
                T::RegisterType(handler);
            }

            return handler;
        }

        template<typename T, typename ...B>
        decltype(auto) Type()
        {
            return Type<T, B...>(GetTypeName<T>());
        }

        template<typename T>
        TypeHandler* FindType()
        {
            return FindTypeById(GetTypeID<T>());
        }

        template<typename T>
        Span<TypeHandler*> FindTypesByAttribute()
        {
            return FindTypesByAttribute(GetTypeID<T>());
        }

        template<typename T>
        Span<FunctionHandler*> FindFunctionsByAttribute()
        {
            return FindFunctionsByAttribute(GetTypeID<T>());
        }

        template<auto Func>
        decltype(auto) Function(const StringView& name)
        {
            using FuncType = Traits::RemoveConstFunc<decltype(Func)>;
            using DecompType = MemberFunctionTemplateDecomposer<Func, FuncType>;
            return DecompType::CreateHandler(NewFunction(DecompType::MakeCreation(name)));
        }

        template<typename T>
        Array<T*> InstantiateDerived()
        {
            Array<T*> ret;

            if (TypeHandler* baseTypeHandler = FindType<T>())
            {
                Span<DerivedType> derivedTypes = baseTypeHandler->GetDerivedTypes();
                for (const DerivedType& derivedType : derivedTypes)
                {
                    if (TypeHandler* type = FindTypeById(derivedType.typeId))
                    {
                        if (T* instance = type->Cast<T>(type->NewInstance()))
                        {
                            ret.EmplaceBack(instance);
                        }
                    }
                }
            }
            return ret;
        }

        template<typename T>
        HashMap<TypeID, T*> InstantiateDerivedAsMap()
        {
            HashMap<TypeID, T*> ret;

            if (TypeHandler* baseTypeHandler = FindType<T>())
            {
                Span<DerivedType> derivedTypes = baseTypeHandler->GetDerivedTypes();
                for (const DerivedType& derivedType : derivedTypes)
                {
                    if (TypeHandler* type = FindTypeById(derivedType.typeId))
                    {
                        if (T* instance = type->Cast<T>(type->NewInstance()))
                        {
                            ret.Insert(type->GetTypeInfo().typeId, instance);
                        }
                    }
                }
            }
            return ret;
        }

    }
}